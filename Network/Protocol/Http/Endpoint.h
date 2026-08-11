//
// Created by hscloud on 26. 7. 20.
//

#pragma once
#include "Base/Type.h"

namespace ne::network::http
{
	/**
	 * @class Endpoint
	 * @brief 접속 대상 서버(호스트/포트/TLS 여부)를 나타내는 값 타입입니다.
	 *
	 * host:port + TLS 여부는 프로토콜 버전과 무관하므로(HTTP/1.1·HTTP/2 공용) http(버전 무관) 계층에
	 * 둡니다. URL 문자열 파싱 없이 접속 정보를 이미 알고 있는 경우(내부 서비스 호출, 테스트 등)에 씁니다.
	 */
	struct Endpoint
	{
		string_t host;
		uint16_t port;
		bool_t isSecure{ false };
	};
}
