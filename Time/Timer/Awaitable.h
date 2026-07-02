//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <coroutine>
#include "Clock.h"
#include "TimerWheel.h"
#include "Type.h"

BEGIN_NS(ne::time)
	// TimerWheel 기반 타이머 awaitable.
	// thread detach 방식 대신 이벤트 루프의 Tick()과 연동.
	class Awaitable
	{
	public:
		Awaitable(TimerWheel& _wheel, const std::chrono::milliseconds _duration) noexcept
			: wheel(_wheel)
			, duration(_duration) {}

		NEBULA_NON_COPYABLE_MOVABLE(Awaitable)

	private:
		TimerWheel& wheel;
		std::chrono::milliseconds duration;
		uint64_t timerId{0};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept
		{
			return duration.count() <= 0;
		}

		void await_suspend(std::coroutine_handle<> _handle)
		{
			timerId = wheel.Schedule(duration, [_handle]() mutable { _handle.resume(); });
		}

		void await_resume() const noexcept {}
	};

	[[nodiscard]] inline Awaitable SleepFor(TimerWheel& _wheel, const std::chrono::milliseconds _duration) { return Awaitable{ _wheel, _duration }; }

	[[nodiscard]] inline Awaitable Deadline(TimerWheel& _wheel, const std::chrono::steady_clock::time_point _timePoint)
	{
		const auto now = std::chrono::steady_clock::now();
		const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(_timePoint - now);

		return Awaitable{ _wheel, diff.count() > 0 ? diff : std::chrono::milliseconds{ 0 } };
	}
END_NS
