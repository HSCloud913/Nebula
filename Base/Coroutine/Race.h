//
// Created by hscloud on 26. 7. 24.
//

#pragma once
#include <coroutine>
#include "Base/Type.h"

namespace ne
{
	/**
	 * @class RaceState
	 * @brief 여러 레이서(코루틴) 중 먼저 끝난 하나를 가리기 위한 경합 상태입니다.
	 *
	 * 승리한 레이서가 isDecided 를 세팅하고, outer(경합을 기다리는 콤비네이터 코루틴 핸들)가
	 * 등록돼 있으면 그것을 깨웁니다. WhenAny()(Base)·Timeout()(Io) 등 경합 콤비네이터가 공유하는
	 * 빌딩 블록이라 공개 표면에 둡니다 — 과거 Internal/ 에 있었지만 공개 헤더(WhenAny.h)가
	 * include 하고 Io 가 외부에서 참조해 위치가 실체와 어긋났습니다(2026-08-06 승격).
	 *
	 * @note 단일 스레드 실행자 위에서의 구동을 전제하므로 원자적 동기화가 필요 없습니다.
	 */
	struct RaceState
	{
		std::coroutine_handle<> outer;
		bool_t isDecided{ false };
	};

	/**
	 * @class AwaitDecision
	 * @brief 경합 콤비네이터가 승부 결과를 기다리기 위해 co_await 하는 대기점입니다.
	 *
	 * RaceState 가 이미 결정된 상태면 즉시 통과하고, 아니면 자신의 핸들을 RaceState 에 남기고
	 * suspend 합니다. 이후 승리한 레이서가 outer 를 Post 로 재개시킵니다.
	 */
	struct AwaitDecision
	{
		RaceState& state;

		[[nodiscard]] bool await_ready() const noexcept { return state.isDecided; }
		void await_suspend(const std::coroutine_handle<> _handle) noexcept { state.outer = _handle; }
		void await_resume() const noexcept {}
	};
}
