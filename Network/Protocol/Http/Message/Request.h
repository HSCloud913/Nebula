//
// Created by hscloud on 26. 7. 20.
//

#pragma once
#include "Base/Type.h"
#include "Network/Protocol/Http/Message/Method.h"
#include "Network/Protocol/Http/Message/Header.h"
#include "Network/Protocol/Http/Message/Body.h"

namespace ne::network::http
{
	/**
	 * @class Request
	 * @brief HTTP 요청 메시지입니다. 버전(HTTP/1.1 등)에 무관한 공용 표현입니다.
	 *
	 * target 은 요청 라인의 request-target 전체(예: "/path?query")이며, 스킴/호스트는 포함하지
	 * 않습니다(호스트는 Host 헤더로 별도 전달).
	 */
	struct Request
	{
		Method method{ Method::GET };
		string_t target{ "/" };
		Headers headers;
		Body body;
	};
}
