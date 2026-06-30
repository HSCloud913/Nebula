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
	class TimerAwaitable
	{
	public:
		TimerAwaitable(TimerWheel& _wheel, const Duration _duration) noexcept
			: wheel(_wheel)
			, duration(_duration) {}
		NEBULA_NON_COPYABLE_MOVABLE(TimerAwaitable)

	private:
		TimerWheel& wheel;
		Duration duration;
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

	[[nodiscard]] inline TimerAwaitable SleepFor(TimerWheel& _wheel, const Duration _duration) { return TimerAwaitable{ _wheel, _duration }; }

	[[nodiscard]] inline TimerAwaitable Deadline(TimerWheel& _wheel, const TimePoint _timePoint)
	{
		const auto now = Now();
		const auto diff = std::chrono::duration_cast<Duration>(_timePoint - now);

		return TimerAwaitable{ _wheel, diff.count() > 0 ? diff : Duration{ 0 } };
	}
END_NS
