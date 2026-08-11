//
// Created by hscloud on 26. 7. 29.
//

#pragma once
#include <coroutine>
#include <exception>
#include "Base/Type.h"

namespace ne
{
	/**
	 * @class Detached
	 * @brief 소유자 없이 스스로 완주하고 프레임을 자동 파괴하는 fire-and-forget 코루틴 타입입니다.
	 *
	 * Task 와 달리 즉시 시작하고(initial_suspend = never) 완료 시 프레임이 자동 파괴됩니다
	 * (final_suspend = never). co_await 할 수 없고 결과도 없습니다. 첫 suspend 지점에서 호출자에게
	 * 제어가 돌아오며, 이후 재개는 awaitable 의 완료 경로(이벤트 루프)가 담당합니다. 정리 작업을
	 * 이벤트 루프에 위임하고 곧바로 돌아와야 하는 소멸자 등에서 씁니다.
	 *
	 * @note 재개를 담당할 루프가 더 이상 돌지 않으면 suspend 된 프레임이 남습니다(누수 — 파괴로 인한
	 *       크래시보다 안전한 쪽을 택한 것). 확실한 완료가 필요하면 Task 를 소유하고 co_await 하세요.
	 */
	struct Detached
	{
		struct promise_type
		{
			Detached get_return_object() noexcept { return {}; }

			std::suspend_never initial_suspend() noexcept { return {}; }
			std::suspend_never final_suspend() noexcept { return {}; }

			void return_void() noexcept {}
			void unhandled_exception() noexcept { std::terminate(); }
		};
	};
}
