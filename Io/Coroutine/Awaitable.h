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

#if defined(IS_POSIX)
#   include "Engine/IoUring/IoUringEngine.h"
#elif defined(_WIN32)
#   include "Engine/Iocp/IocpEngine.h"
#endif

BEGIN_NS(ne::io)
	// ── 소켓 이벤트 Awaitable ─────────────────────────────────────────────────

	// 소켓 쓰기 준비 대기.
	// co_await → Result<uint32_t, OsError> : 발생한 IoEvent 플래그
	class SendAwaitable
	{
	public:
		SendAwaitable(const socket_t _fd, IIoEngine& _engine) noexcept
			: fd(_fd)
			, engine(_engine) {}

	private:
		socket_t fd;
		IIoEngine& engine;
		uint32_t triggeredEvents{};
		std::optional<ne::OsError> watchError;

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			auto result = engine.Watch(fd, IoEvent::Write | IoEvent::Error,
									[this, _handle](socket_t _triggeredFd, uint32_t _events) mutable
									{
										(void)engine.Unwatch(_triggeredFd, IoEvent::Write); // Read 방향(동시 ReceiveAwaitable)에는 영향 없음
										triggeredEvents = _events;
										_handle.resume();
									});
			if (result.IsError())
			{
				watchError.emplace(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<uint32_t, ne::OsError> await_resume() noexcept
		{
			if (watchError)
				return ne::Result<uint32_t, ne::OsError>::Error(std::move(*watchError));

			return ne::Result<uint32_t, ne::OsError>::Ok(triggeredEvents);
		}
	};

	// 소켓 읽기 준비 대기.
	// co_await → Result<uint32_t, OsError> : 발생한 IoEvent 플래그
	class ReceiveAwaitable
	{
	public:
		ReceiveAwaitable(const socket_t _fd, IIoEngine& _engine) noexcept
			: fd(_fd)
			, engine(_engine) {}

	private:
		socket_t fd;
		IIoEngine& engine;
		uint32_t triggeredEvents{};
		std::optional<ne::OsError> watchError;

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			auto result = engine.Watch(fd, IoEvent::Read | IoEvent::HangUp | IoEvent::Error,
									[this, _handle](socket_t _triggeredFd, uint32_t _events) mutable
									{
										(void)engine.Unwatch(_triggeredFd, IoEvent::Read); // Write 방향(동시 SendAwaitable)에는 영향 없음
										triggeredEvents = _events;
										_handle.resume();
									});
			if (result.IsError())
			{
				watchError.emplace(std::move(result.Error()));
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

	// ── 소켓 Proactor Awaitable ──────────────────────────────────────────────
	// IIoEngine::SubmitSend/SubmitReceive 사용 — 플랫폼 독립(Epoll/IoUring/Iocp 모두
	// socket_t 로 구현). Network/Ipc 양쪽이 동일하게 필요로 해서 여기 공용으로 둔다 —
	// 각자 도메인 어휘(recv/send 이름 외엔 socket_t/IIoEngine 밖에 안 씀)가 없는 순수
	// 엔진 래퍼라 모듈별로 복제해 둘 이유가 없다.

	// 소켓 proactor 송신 대기.
	// co_await → Result<size_t, OsError>
	class SendSubmitAwaitable
	{
	public:
		SendSubmitAwaitable(IIoEngine& _engine, const socket_t _fd, const void* _buffer, const std::size_t _length) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, length(_length) {}

	private:
		IIoEngine& engine;
		socket_t fd;
		const void* buffer;
		std::size_t length;
		IoContext context{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			context.handle = _handle;

			if (auto result = engine.SubmitSend(fd, buffer, length, &context); result.IsError())
			{
				context.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return std::move(context.result); }
	};

	// 소켓 proactor 수신 대기.
	// co_await → Result<size_t, OsError>
	class ReceiveSubmitAwaitable
	{
	public:
		ReceiveSubmitAwaitable(IIoEngine& _engine, const socket_t _fd, void* _buffer, const std::size_t _length) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, length(_length) {}

	private:
		IIoEngine& engine;
		socket_t fd;
		void* buffer;
		std::size_t length;
		IoContext context{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			context.handle = _handle;

			if (auto result = engine.SubmitReceive(fd, buffer, length, &context); result.IsError())
			{
				context.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return std::move(context.result); }
	};

	// ── 파일 I/O Awaitable ────────────────────────────────────────────────────
	// SubmitRead/SubmitWrite 는 IIoEngine 외부 메서드이므로 구체 엔진 타입을 참조.
	// context 타입은 IoContext 로 통일 (Windows: OVERLAPPED 포함, Linux: 미포함).

#if defined(_WIN32)
	// 파일 읽기 비동기 대기 (IOCP).
	// co_await → Result<size_t, OsError>
	class ReadSubmitAwaitable
	{
	public:
		ReadSubmitAwaitable(IocpEngine& _engine, const file_t _fd, const std::span<ne::byte_t> _buffer, const std::size_t _offset) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, offset(_offset) {}

	private:
		IocpEngine& engine;
		file_t fd;
		std::span<ne::byte_t> buffer;
		std::size_t offset;
		IoContext context{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			context.handle = _handle;

			auto result = engine.SubmitRead(fd, buffer.data(), buffer.size(), offset, &context);
			if (result.IsError())
			{
				context.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return std::move(context.result); }
	};

	// 파일 쓰기 비동기 대기 (IOCP).
	// co_await → Result<size_t, OsError>
	class WriteSubmitAwaitable
	{
	public:
		WriteSubmitAwaitable(IocpEngine& _engine, const file_t _fd, const std::span<const ne::byte_t> _buffer, const std::size_t _offset) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, offset(_offset) {}

	private:
		IocpEngine& engine;
		file_t fd;
		std::span<const ne::byte_t> buffer;
		std::size_t offset;
		IoContext context{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			context.handle = _handle;

			if (auto result = engine.SubmitWrite(fd, buffer.data(), buffer.size(), offset, &context); result.IsError())
			{
				context.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return std::move(context.result); }
	};
#elif defined(IS_POSIX)
	// 파일 읽기 비동기 대기 (io_uring).
	// co_await → Result<size_t, OsError>
	class ReadSubmitAwaitable
	{
	public:
		ReadSubmitAwaitable(IoUringEngine& _engine, const file_t _fd, const std::span<ne::byte_t> _buffer, const std::size_t _offset) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, offset(_offset) {}

	private:
		IoUringEngine& engine;
		file_t fd;
		std::span<ne::byte_t> buffer;
		std::size_t offset;
		IoContext context{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			context.handle = _handle;

			if (auto result = engine.SubmitRead(fd, buffer.data(), buffer.size(), offset, &context); result.IsError())
			{
				context.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return std::move(context.result); }
	};

	// 파일 쓰기 비동기 대기 (io_uring).
	// co_await → Result<size_t, OsError>
	class WriteSubmitAwaitable
	{
	public:
		WriteSubmitAwaitable(IoUringEngine& _engine, const file_t _fd, const std::span<const ne::byte_t> _buffer, const std::size_t _offset) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, offset(_offset) {}

	private:
		IoUringEngine& engine;
		file_t fd;
		std::span<const ne::byte_t> buffer;
		std::size_t offset;
		IoContext context{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			context.handle = _handle;

			if (auto result = engine.SubmitWrite(fd, buffer.data(), buffer.size(), offset, &context); result.IsError())
			{
				context.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return std::move(context.result); }
	};

#endif // platform

END_NS
