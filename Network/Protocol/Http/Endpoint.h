//
// Created by hscloud on 26. 7. 20.
//

#pragma once
#include <string>
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

	/**
	 * @brief 이 엔드포인트의 authority 문자열(HTTP/1.1 의 `Host`, HTTP/2 의 `:authority`)을 만듭니다.
	 *
	 * 기본 포트(http 80 / https 443)면 포트를 생략하고, 그 외에는 `host:port` 로 붙입니다. RFC 9112 §3.2
	 * 는 비기본 포트를 반드시 포함하도록 요구합니다 — 생략하면 서버가 다른 vhost 로 라우팅하거나 엄격한
	 * 게이트웨이가 400 을 돌려줍니다.
	 */
	[[nodiscard]] inline string_t Authority(const Endpoint& _endpoint)
	{
		const uint16_t defaultPort = _endpoint.isSecure ? 443 : 80;
		if (_endpoint.port == defaultPort) return _endpoint.host;

		return _endpoint.host + ":" + std::to_string(_endpoint.port);
	}
}
