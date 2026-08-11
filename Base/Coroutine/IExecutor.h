//
// Created by hscloud on 26. 8. 2.
//

#pragma once
#include <coroutine>
#include "Base/Type.h"

namespace ne
{
	/**
	 * @class IExecutor
	 * @brief 코루틴 핸들의 재개를 루프에 예약할 수 있는 실행자 계약입니다.
	 *
	 * Event/WhenAny 같은 범용 코루틴 프리미티브가 "다음 tick 에 재개"(지연 재개)를 요청할 때 쓰는
	 * 최소 인터페이스입니다. 구체 이벤트 루프(ne::io::Context 등)가 이를 구현하므로, Base 계층은
	 * Io 를 몰라도 지연 재개를 표현할 수 있습니다(계층 역전/순환 의존 방지 경계).
	 */
	class IExecutor
	{
	public:
		IExecutor() = default;
		virtual ~IExecutor() = default;

		NEBULA_NON_COPYABLE_MOVABLE(IExecutor)

	public:
		/** @brief _handle 의 재개를 실행자 루프에 예약합니다 — 호출 스택 안에서 즉시 재개하지 않습니다. */
		virtual void_t Post(std::coroutine_handle<> _handle) = 0;
	};
}
