//
// Created by hscloud on 26. 7. 20.
//

#pragma once
#include "Base/Type.h"

namespace ne::network::http
{
	// 표준 상태 코드의 기본 reason phrase. 모르는 코드면 빈 문자열을 반환한다(호출측이 직접 넘긴 값을 우선 사용해야 함).
	[[nodiscard]] string_view_t DefaultReasonPhrase(int_t _statusCode) noexcept;
}
