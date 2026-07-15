//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <coroutine>
#include "Time/Timer/TimerWheel.h"
#include "Base/Type.h"

BEGIN_NS(ne::time)
	/**
	 * @class Awaitable
	 * @brief TimerWheel 기반의 타이머 awaitable입니다.
	 *
	 * 별도 스레드를 detach하는 방식 대신, 이벤트 루프의 Tick()과 연동해 co_await 시점에
	 * 타이머를 등록하고 만료 시 코루틴을 resume합니다.
	 */
	class Awaitable
	{
	public:
		Awaitable(TimerWheel& _wheel, const std::chrono::milliseconds _duration) noexcept
			: wheel(_wheel)
			, duration(_duration) {}

		/**
		 * @brief 예약된 타이머를 취소해 use-after-free를 방지합니다.
		 *
		 * 코루틴 프레임이 타이머 만료 전에 파괴되면(취소/타임아웃/예외로 상위 Task 폐기), 스케줄된
		 * 콜백이 이미 파괴된 handle을 resume해 use-after-free가 됩니다. 소멸 시 예약 타이머를
		 * 취소해 콜백이 절대 실행되지 않게 합니다. 이미 발화됐다면 Cancel은 no-op입니다(그 id는
		 * Tick이 live에서 이미 제거).
		 */
		~Awaitable() { if (timerId != 0) wheel.Cancel(timerId); }

		NEBULA_NON_COPYABLE_MOVABLE(Awaitable)

	private:
		TimerWheel& wheel;
		std::chrono::milliseconds duration;
		uint64_t timerId{ 0 };

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return duration.count() <= 0; }

		void_t await_suspend(std::coroutine_handle<> _handle) { timerId = wheel.Schedule(duration, [_handle]() mutable { _handle.resume(); }); }

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
