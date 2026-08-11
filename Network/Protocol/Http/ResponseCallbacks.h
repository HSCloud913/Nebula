//
// Created by hscloud on 26. 7. 24.
//

#pragma once
#include <cstddef>
#include <functional>
#include <span>
#include "Base/Type.h"
#include "Network/Protocol/Http/Message/Header.h"

namespace ne::network::http
{
	/**
	 * @class ResponseCallbacks
	 * @brief 응답을 전부 버퍼링하지 않고 조각 단위로 흘려 받기 위한 콜백 묶음입니다.
	 *
	 * 상태줄+헤더가 준비되면 onHead 를 1회 호출하고, 본문을 읽는 대로 onBody 를 조각마다 호출합니다
	 * (Content-Length/chunked 무관). 대용량 다운로드나 SSE 처럼 본문 전체를 메모리에 올리고 싶지 않을 때
	 * 씁니다. 상태/헤더/본문 조각 개념은 프로토콜 버전과 무관하므로(HTTP/1.1·HTTP/2 공용) http 계층에 둡니다.
	 *
	 * @note 콜백이 false 를 반환하면 수신을 즉시 중단합니다. onBody 의 span 은 콜백이 실행되는 동안에만
	 *       유효합니다 — 이후에도 보관하려면 콜백 안에서 복사하세요.
	 */
	struct ResponseCallbacks
	{
		// 상태줄 + 헤더가 준비되면 1회 호출. false 반환 시 본문을 읽지 않고 중단.
		std::function<bool_t(int_t _statusCode, string_view_t _reason, const Headers& _headers)> onHead;

		// 본문 조각마다 호출. false 반환 시 조기 중단.
		std::function<bool_t(std::span<const byte_t> _chunk)> onBody;
	};
}
