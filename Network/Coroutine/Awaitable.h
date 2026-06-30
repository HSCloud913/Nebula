//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <coroutine>
#include <optional>
#include "IoEngine/IIoEngine.h"
#include "Result.h"
#include "Error.h"

BEGIN_NS(ne::network)
	// 소켓 읽기 준비 대기.
	// co_await → Result<uint32_t, OsError> : 실제 발생한 IoEvent 플래그
	class RecvAwaitable
	{
	public:
		RecvAwaitable(socket_t _fd, IIoEngine& _engine) noexcept
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
			auto res = engine.Watch(
				fd,
				IoEvent::Read | IoEvent::HangUp | IoEvent::Error,
				[this, _handle](socket_t _fd, uint32_t _events) mutable
				{
					(void)engine.Unwatch(_fd);
					triggeredEvents = _events;
					_handle.resume();
				});

			if (res.IsError())
			{
				watchError.emplace(std::move(res.Error()));
				return false; // Watch 실패 → 즉시 재개, await_resume 에서 에러 반환
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
		socket_t fd;
		IIoEngine& engine;
		uint32_t triggeredEvents{};
		std::optional<ne::OsError> watchError;

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			auto res = engine.Watch(
				fd,
				IoEvent::Write | IoEvent::Error,
				[this, _handle](socket_t _fd, uint32_t _events) mutable
				{
					(void)engine.Unwatch(_fd);
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


END_NS

// TimerAwaitable 은 Time/TimerAwaitable.h 로 이전.
// 타이머가 필요할 때: #include "TimerAwaitable.h"  (ne::time::SleepFor / Deadline)

