//
// Created by hscloud on 26. 7. 24.
//

#pragma once
#include <chrono>
#include <memory>
#include "Base/Type.h"
#include "Base/Coroutine/Task.h"
#include "Io/Context.h"
#include "Io/Engine.h"

namespace ne::time
{
	class TimerWheel;
}

namespace ne::io
{
	/**
	 * @class Runtime
	 * @brief 단일 스레드 I/O 런타임 파사드입니다. 엔진·타이머 휠·Context 를 RAII 하나로 묶습니다.
	 *
	 * EngineType 만 주면 MakeEngine 으로 플랫폼 최적 엔진을 골라 Context 를 구성합니다. 가장 흔한
	 * 사용은 BlockOn(task) — 주어진 코루틴을 이 Context 위에서 완료까지 구동하고 결과를 반환합니다
	 * (별도 스레드를 쓰지 않아 결정론적). 여러 코어로 확장하려면 ContextPool 을 직접 쓰세요.
	 *
	 * @note 코루틴/소켓은 GetContext() 로 얻은 이 Context 를 써야 합니다. move/copy 불가(고정).
	 */
	class Runtime
	{
	public:
		explicit Runtime(EngineType _type = EngineType::PROACTOR);
		~Runtime();

		NEBULA_NON_COPYABLE_MOVABLE(Runtime)

	private:
		std::unique_ptr<IEngine> engine;
		std::unique_ptr<ne::time::TimerWheel> timerWheel;
		std::unique_ptr<Context> context;

	public:
		[[nodiscard]] Context& GetContext() noexcept { return *context; }
		[[nodiscard]] bool_t IsValid() const noexcept;

	public:
		/**
		 * @brief _task 를 이 런타임의 Context 위에서 완료될 때까지 구동하고 co_return 값을 반환합니다.
		 * @note 호출 스레드를 블록합니다. _task 가 끝나지 않으면 반환하지 않으니, 시한이 필요하면
		 *       Timeout() 콤비네이터로 감싼 태스크를 넘기세요.
		 */
		template <typename T>
		T BlockOn(ne::Task<T> _task)
		{
			_task.Resume();
			while (!_task.IsReady()) (void_t)context->RunOnce(std::chrono::milliseconds{ 50 });
			return _task.await_resume();
		}
	};
}
