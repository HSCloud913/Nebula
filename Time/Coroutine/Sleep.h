//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <coroutine>
#include "Time/Deadline.h"
#include "Time/TimerQueue.h"
#include "Base/Type.h"

namespace ne::time
{
	/**
	 * @class Timer
	 * @brief TimerQueue 에 예약된 타이머 하나를 co_await 로 기다리는 awaitable 입니다.
	 *
	 * 별도 스레드를 detach 하는 방식 대신, 이벤트 루프의 Tick() 과 연동해 co_await 시점에
	 * 타이머를 등록하고 만료 시 코루틴을 resume 합니다. 직접 생성하지 않고 SleepFor()/SleepUntil()
	 * 으로 만듭니다.
	 */
	class Timer
	{
	public:
		Timer(TimerQueue& _queue, const std::chrono::milliseconds _duration) noexcept
			: queue(_queue)
			, duration(_duration) {}

		/**
		 * @brief 예약된 타이머를 취소해 use-after-free를 방지합니다.
		 *
		 * 코루틴 프레임이 타이머 만료 전에 파괴되면(취소/타임아웃/예외로 상위 Task 폐기), 스케줄된
		 * 콜백이 이미 파괴된 handle을 resume해 use-after-free가 됩니다. 소멸 시 예약 타이머를
		 * 취소해 콜백이 절대 실행되지 않게 합니다. 이미 발화됐다면 Cancel은 no-op입니다. (그 id는
		 * Tick이 live에서 이미 제거)
		 */
		~Timer() { if (timerId != 0) queue.Cancel(timerId); }

		NEBULA_NON_COPYABLE_MOVABLE(Timer)

	private:
		TimerQueue& queue;
		std::chrono::milliseconds duration;
		uint64_t timerId{ 0 };

	public:
		[[nodiscard]] bool await_ready() const noexcept { return duration.count() <= 0; }

		void await_suspend(std::coroutine_handle<> _handle) { timerId = queue.Schedule(duration, [_handle]() mutable { _handle.resume(); }); }

		void await_resume() const noexcept {}
	};

	[[nodiscard]] inline Timer SleepFor(TimerQueue& _queue, const std::chrono::milliseconds _duration) { return Timer{ _queue, _duration }; }

	/**
	 * @brief _timePoint 까지 대기하는 awaitable 을 만듭니다(SleepFor 의 절대시각 버전).
	 *
	 * @note 남은 시간은 **큐의 시계**(_queue.Now()) 기준으로 계산해야 합니다. 예전에는
	 * steady_clock::now() 를 직접 써서, 테스트가 페이크 클럭을 주입하면 기준이 서로 달라
	 * (_timePoint - 실제현재) 라는 무의미한 지연이 나왔습니다.
	 */
	[[nodiscard]] inline Timer SleepUntil(TimerQueue& _queue, const std::chrono::steady_clock::time_point _timePoint)
	{
		const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(_timePoint - _queue.Now());

		return Timer{ _queue, diff.count() > 0 ? diff : std::chrono::milliseconds{ 0 } };
	}

	/** @brief Deadline 값 타입으로 대기합니다(무기한 데드라인이면 절대 깨어나지 않는 대신 즉시 완료로 취급). */
	[[nodiscard]] inline Timer SleepUntil(TimerQueue& _queue, const Deadline _deadline) { return Timer{ _queue, _deadline.Remaining(_queue.Now()) }; }
}
