//
// Created by hscloud on 26. 7. 30.
//

#pragma once
#include "Base/Type.h"

namespace ne::network::http
{
	/**
	 * @class Version
	 * @brief HTTP 프로토콜 버전 선택입니다(ServerBuilder/ClientBuilder 공용).
	 *
	 * AUTO 는 TLS 경로에서 ALPN 협상 결과("h2" 면 HTTP/2, 아니면 HTTP/1.1)를 따르고, 평문 경로에서는
	 * HTTP/1.1 을 씁니다(평문 HTTP/2 는 prior knowledge 전제라 자동 감지 대상이 아님 — 명시 선택 필요).
	 */
	enum class Version : byte_t
	{
		AUTO,
		HTTP_1_1,
		HTTP_2,
	};
}
