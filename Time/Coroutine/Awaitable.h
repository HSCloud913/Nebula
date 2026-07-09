//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <coroutine>
#include "Time/Timer/TimerWheel.h"
#include "Base/Type.h"

BEGIN_NS(ne::time)
	// TimerWheel 기반 타이머 awaitable.
	// thread detach 방식 대신 이벤트 루프의 Tick()과 연동.
	class Awaitable
	{
	public:
		Awaitable(TimerWheel& _wheel, const std::chrono::milliseconds _duration) noexcept
			: wheel(_wheel)
			, duration(_duration) {}

		// 코루틴 프레임이 타이머 만료 전에 파괴되면(취소/타임아웃/예외로 상위 Task 폐기), 스케줄된 콜백이
		// 이미 파괴된 handle 을 resume 해 use-after-free 가 된다. 소멸 시 예약 타이머를 취소해 콜백이
		// 절대 실행되지 않게 한다. 이미 발화됐다면 Cancel 은 no-op(그 id 는 Tick 이 live 에서 이미 제거).
		~Awaitable()
		{
			if (timerId != 0) wheel.Cancel(timerId);
		}

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

		void_t await_suspend(std::coroutine_handle<> _handle)
		{
			timerId = wheel.Schedule(duration, [_handle]() mutable { _handle.resume(); });
		}

		void_t await_resume() const noexcept {}
	};

	[[nodiscard]] inline Awaitable SleepFor(TimerWheel& _wheel, const std::chrono::milliseconds _duration) { return Awaitable{ _wheel, _duration }; }

	[[nodiscard]] inline Awaitable Deadline(TimerWheel& _wheel, const std::chrono::steady_clock::time_point _timePoint)
	{
		const auto now = std::chrono::steady_clock::now();
		const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(_timePoint - now);

		return Awaitable{ _wheel, diff.count() > 0 ? diff : std::chrono::milliseconds{ 0 } };
	}
END_NS
