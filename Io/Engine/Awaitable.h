//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <coroutine>
#include <optional>
#include <span>
#include <cstddef>
#include "Engine/IIoEngine.h"
#include "IoType.h"
#include "Result.h"
#include "Error.h"
#include "Type.h"

BEGIN_NS(ne::io)

	// ── 소켓 이벤트 Awaitable ─────────────────────────────────────────────────
	// (Network/IoEngine/Awaitable.h 에서 ne::io 로 이동)

	// 소켓 읽기 준비 대기.
	// co_await → Result<uint32_t, OsError> : 실제 발생한 IoEvent 플래그
	class RecvAwaitable
	{
	public:
		RecvAwaitable(socket_t _fd, IIoEngine& _engine) noexcept
			: fd(_fd)
			, engine(_engine) {}

	private:
		socket_t   fd;
		IIoEngine& engine;
		uint32_t   triggeredEvents{};
		std::optional<ne::OsError> watchError;

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			auto res = engine.Watch(
				fd,
				IoEvent::Read | IoEvent::HangUp | IoEvent::Error,
				[this, _handle](socket_t _f, uint32_t _events) mutable
				{
					(void)engine.Unwatch(_f);
					triggeredEvents = _events;
					_handle.resume();
				});

			if (res.IsError())
			{
				watchError.emplace(std::move(res.Error()));
				return false;
			}
			return true;
		}

		[[nodiscard]] ne::Result<uint32_t, ne::OsError> await_resume() noexcept
		{
			if (watchError) return ne::Result<uint32_t, ne::OsError>::Error(std::move(*watchError));
			return ne::Result<uint32_t, ne::OsError>::Ok(triggeredEvents);
		}
	};


	// 소켓 쓰기 준비 대기.
	// co_await → Result<uint32_t, OsError> : 실제 발생한 IoEvent 플래그
	class SendAwaitable
	{
	public:
		SendAwaitable(socket_t _fd, IIoEngine& _engine) noexcept
			: fd(_fd)
			, engine(_engine) {}

	private:
		socket_t   fd;
		IIoEngine& engine;
		uint32_t   triggeredEvents{};
		std::optional<ne::OsError> watchError;

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			auto res = engine.Watch(
				fd,
				IoEvent::Write | IoEvent::Error,
				[this, _handle](socket_t _f, uint32_t _events) mutable
				{
					(void)engine.Unwatch(_f);
					triggeredEvents = _events;
					_handle.resume();
				});

			if (res.IsError())
			{
				watchError.emplace(std::move(res.Error()));
				return false;
			}
			return true;
		}

		[[nodiscard]] ne::Result<uint32_t, ne::OsError> await_resume() noexcept
		{
			if (watchError) return ne::Result<uint32_t, ne::OsError>::Error(std::move(*watchError));
			return ne::Result<uint32_t, ne::OsError>::Ok(triggeredEvents);
		}
	};


	// ── 파일 I/O Awaitable ────────────────────────────────────────────────────
	// FileReadAwaitable / FileWriteAwaitable 은 SubmitRead/SubmitWrite 를 호출하므로
	// IIoEngine 인터페이스가 아닌 구체 엔진 타입을 참조한다 (의도된 설계).

#if defined(IS_POSIX)
#   include "Engine/IoUring/IoUringEngine.h"

	// 파일 읽기 비동기 대기 (io_uring).
	// co_await → Result<size_t, OsError> : 읽은 바이트 수
	class FileReadAwaitable
	{
	public:
		FileReadAwaitable(IoUringEngine& _engine, file_t _fd,
		                  std::span<ne::byte_t> _buf, std::size_t _offset) noexcept
			: engine(_engine), fd(_fd), buf(_buf), offset(_offset) {}

	private:
		IoUringEngine&        engine;
		file_t                fd;
		std::span<ne::byte_t> buf;
		std::size_t           offset;
		FileIoCtx             ctx{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			ctx.handle = _handle;
			auto r = engine.SubmitRead(fd, buf.data(), buf.size(), offset, &ctx);
			if (r.IsError())
			{
				ctx.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
				return false;
			}
			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept
		{
			return std::move(ctx.result);
		}
	};

	// 파일 쓰기 비동기 대기 (io_uring).
	// co_await → Result<size_t, OsError> : 쓴 바이트 수
	class FileWriteAwaitable
	{
	public:
		FileWriteAwaitable(IoUringEngine& _engine, file_t _fd,
		                   std::span<const ne::byte_t> _buf, std::size_t _offset) noexcept
			: engine(_engine), fd(_fd), buf(_buf), offset(_offset) {}

	private:
		IoUringEngine&              engine;
		file_t                      fd;
		std::span<const ne::byte_t> buf;
		std::size_t                 offset;
		FileIoCtx                   ctx{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			ctx.handle = _handle;
			auto r = engine.SubmitWrite(fd, buf.data(), buf.size(), offset, &ctx);
			if (r.IsError())
			{
				ctx.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
				return false;
			}
			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept
		{
			return std::move(ctx.result);
		}
	};

#elif defined(_WIN32)
#   include "Engine/Iocp/IocpEngine.h"

	// 파일 읽기 비동기 대기 (IOCP).
	// co_await → Result<size_t, OsError> : 읽은 바이트 수
	class FileReadAwaitable
	{
	public:
		FileReadAwaitable(IocpEngine& _engine, file_t _fd,
		                  std::span<ne::byte_t> _buf, std::size_t _offset) noexcept
			: engine(_engine), fd(_fd), buf(_buf), offset(_offset) {}

	private:
		IocpEngine&           engine;
		file_t                fd;
		std::span<ne::byte_t> buf;
		std::size_t           offset;
		FileIocpCtx           ctx{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			ctx.handle = _handle;
			auto r = engine.SubmitRead(fd, buf.data(), buf.size(), offset, &ctx);
			if (r.IsError())
			{
				ctx.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
				return false;
			}
			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept
		{
			return std::move(ctx.result);
		}
	};

	// 파일 쓰기 비동기 대기 (IOCP).
	// co_await → Result<size_t, OsError> : 쓴 바이트 수
	class FileWriteAwaitable
	{
	public:
		FileWriteAwaitable(IocpEngine& _engine, file_t _fd,
		                   std::span<const ne::byte_t> _buf, std::size_t _offset) noexcept
			: engine(_engine), fd(_fd), buf(_buf), offset(_offset) {}

	private:
		IocpEngine&                 engine;
		file_t                      fd;
		std::span<const ne::byte_t> buf;
		std::size_t                 offset;
		FileIocpCtx                 ctx{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			ctx.handle = _handle;
			auto r = engine.SubmitWrite(fd, buf.data(), buf.size(), offset, &ctx);
			if (r.IsError())
			{
				ctx.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
				return false;
			}
			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept
		{
			return std::move(ctx.result);
		}
	};

#endif // platform

END_NS
