//
// Created by hscloud on 26. 7. 20.
//

#pragma once
#include "Base/Type.h"
#include "Network/Protocol/Http/Message/Header.h"
#include "Network/Protocol/Http/Message/Body.h"
#include "Network/Protocol/Http/Message/Status.h"

namespace ne::network::http
{
	/**
	 * @class Response
	 * @brief HTTP 응답 메시지입니다. 버전(HTTP/1.1 등)에 무관한 공용 표현입니다.
	 *
	 * reason 을 비워두면 직렬화 시점(HTTP/1.1 의 SerializeStatusLine)에 statusCode 에 맞는 표준
	 * reason phrase 를 자동으로 채웁니다(Message/Status.h 의 DefaultReasonPhrase 참고).
	 */
	struct Response
	{
		int_t statusCode{ 200 };
		string_t reason;
		Headers headers;
		Body body;

		// 흔한 응답 형태를 위한 정적 팩토리. Content-Type 을 자동으로 설정한다.
		[[nodiscard]] static Response Text(const int_t _statusCode, const string_view_t _text)
		{
			Response response;
			response.statusCode = _statusCode;
			response.body = Body::FromString(_text);
			response.headers.Set("Content-Type", "text/plain; charset=utf-8");

			return response;
		}

		[[nodiscard]] static Response Json(const int_t _statusCode, const string_view_t _json)
		{
			Response response;
			response.statusCode = _statusCode;
			response.body = Body::FromString(_json);
			response.headers.Set("Content-Type", "application/json");

			return response;
		}

		// 본문 없는 상태 응답(예: 204, 304, 4xx/5xx 에러).
		[[nodiscard]] static Response Status(const int_t _statusCode)
		{
			Response response;
			response.statusCode = _statusCode;

			return response;
		}
	};
}
