//
// Created by hscloud on 26. 7. 29.
//

#pragma once
#include <coroutine>
#include "Base/Type.h"
#include "Base/Coroutine/IExecutor.h"

namespace ne
{
	/**
	 * @class Event
	 * @brief 단일 소비자용 수동 완료 awaitable(단일 스레드 전용).
	 *
	 * 생산자 코루틴이 Signal()/SignalDeferred() 로 알리고, 소비자 코루틴이 co_await 로 대기합니다.
	 * 단일 스레드 실행자 위에서 구동됨을 전제하므로 락이 필요 없습니다. Signal() 은 대기 중인
	 * 코루틴을 그 자리에서(호출 스택에 중첩되어) 재개합니다.
	 *
	 * @note 한 번에 하나의 대기자만 지원합니다. Signal() 이 co_await 보다 먼저 오면 그 상태를
	 *       기억해 다음 co_await 가 즉시 통과합니다. 신호가 조건보다 먼저 소비될 수 있는 경로라면
	 *       `while (조건) co_await event;` 처럼 조건을 재확인하는 루프로 대기하세요.
	 */
	class Event
	{
	public:
		Event() = default;
		NEBULA_NON_COPYABLE_MOVABLE(Event)

	private:
		std::coroutine_handle<> waiter{};
		bool_t signaled{ false };

	public:
		/**
		 * @class Awaiter
		 * @brief co_await 표현식마다 만들어지는 대기점입니다 — 코루틴 프레임 안에 살며, 소멸 시 대기 등록을 해제합니다.
		 *
		 * Event 자신이 awaiter 였을 때는 프레임이 대기 중 파괴되면 Event 가 파괴된 프레임을 계속
		 * 가리켜, 이후 Signal() 이 죽은 코루틴을 재개했습니다(WhenAny 가 진 레이서를 파괴해 취소하는
		 * 방식과 정면으로 충돌). 등록 해제를 이 객체의 소멸자에 묶어 그 위험을 없앱니다.
		 */
		class Awaiter
		{
		public:
			explicit Awaiter(Event& _event) noexcept
				: event(_event) {}

			~Awaiter()
			{
				// 우리가 등록한 핸들이 아직 걸려 있다면(= 재개 없이 프레임이 파괴되는 중) 해제한다.
				if (registered && event.waiter == registered) event.waiter = {};
			}

			NEBULA_NON_COPYABLE_MOVABLE(Awaiter)

		private:
			Event& event;
			std::coroutine_handle<> registered{};

		public:
			[[nodiscard]] bool await_ready() const noexcept { return event.signaled; }

			void await_suspend(const std::coroutine_handle<> _handle) noexcept
			{
				registered = _handle;
				event.waiter = _handle;
			}

			void await_resume() noexcept
			{
				registered = {}; // 재개됐으므로 소멸자가 해제할 것이 없다
				event.signaled = false;
			}
		};

		[[nodiscard]] Awaiter operator co_await() noexcept { return Awaiter{ *this }; }

	public:
		/** @brief 대기 중인 코루틴을 그 자리에서(동기) 재개합니다(없으면 신호 상태만 기록). */
		void_t Signal() noexcept
		{
			signaled = true;
			if (waiter)
			{
				const auto handle = waiter;
				waiter = {};
				handle.resume();
			}
		}

		/**
		 * @brief 대기 중인 코루틴의 재개를 _executor 루프에 예약합니다(지연 재개).
		 *
		 * 동기 재개(Signal)를 쓰면 소비자 코루틴의 연속(continuation)이 생산자 스택 안에서 실행되어
		 * 깊은 재진입을 만듭니다(생산자 프레임을 소비자가 파괴하는 등). Post 로 지연시키면 생산자는
		 * 곧장 루프로 복귀하고, 다음 tick 에서 continuation 이 독립적으로 재개됩니다.
		 */
		void_t SignalDeferred(IExecutor& _executor)
		{
			signaled = true;
			if (waiter)
			{
				const auto handle = waiter;
				waiter = {};
				_executor.Post(handle);
			}
		}
	};
}
