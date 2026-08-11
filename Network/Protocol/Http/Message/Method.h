//
// Created by hscloud on 26. 7. 20.
//

#pragma once
#include "Base/Type.h"

namespace ne::network::http
{
	/**
	 * @class Method
	 * @brief HTTP 요청 메서드를 나타내는 열거형입니다.
	 *
	 * @note DELETE 는 winnt.h 의 액세스 권한 매크로(`DELETE`)와 충돌하므로 DELETE_ 로 표기합니다.
	 */
	enum class Method : byte_t
	{
		GET,
		POST,
		PUT,
		DELETE_,
		HEAD,
		OPTIONS,
		PATCH,
		CONNECT,
		TRACE,
		UNKNOWN,
	};

	[[nodiscard]] string_view_t ToString(Method _method) noexcept;
	[[nodiscard]] Method MethodFromString(string_view_t _text) noexcept;
}
