//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <coroutine>
#include <optional>
#include <cstddef>
#include "Engine/IIoEngine.h"

BEGIN_NS(ne::io)

	// ── 소켓 이벤트 Awaitable ─────────────────────────────────────────────────

	// 소켓 읽기 준비 대기.
	// co_await → Result<uint32_t, OsError> : 발생한 IoEvent 플래그
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
	// co_await → Result<uint32_t, OsError> : 발생한 IoEvent 플래그
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


	// ── 소켓 Proactor Awaitable ──────────────────────────────────────────────
	// IIoEngine::SubmitReceive / SubmitSend 사용 — 플랫폼 독립.

	// 소켓 proactor 수신 대기.
	// co_await → Result<size_t, OsError>
	class SocketRecvAwaitable
	{
	public:
		SocketRecvAwaitable(IIoEngine& _engine, socket_t _fd, void* _buf, std::size_t _len) noexcept
			: engine(_engine), fd(_fd), buf(_buf), len(_len) {}

	private:
		IIoEngine&  engine;
		socket_t    fd;
		void*       buf;
		std::size_t len;
		IoContext       ctx{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			ctx.handle = _handle;
			auto r = engine.SubmitReceive(fd, buf, len, &ctx);
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

	// 소켓 proactor 송신 대기.
	// co_await → Result<size_t, OsError>
	class SocketSendAwaitable
	{
	public:
		SocketSendAwaitable(IIoEngine& _engine, socket_t _fd, const void* _buf, std::size_t _len) noexcept
			: engine(_engine), fd(_fd), buf(_buf), len(_len) {}

	private:
		IIoEngine&  engine;
		socket_t    fd;
		const void* buf;
		std::size_t len;
		IoContext       ctx{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			ctx.handle = _handle;
			auto r = engine.SubmitSend(fd, buf, len, &ctx);
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

END_NS
