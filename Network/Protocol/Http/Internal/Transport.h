//
// Created by hscloud on 26. 7. 24.
//

#pragma once
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>
#include "Base/Type.h"
#include "Base/Coroutine/Task.h"
#include "Network/Stream/IStream.h"
#include "Network/Protocol/Http/Diagnostic/Error.h"
#include "Network/Protocol/Http/Message/Request.h"
#include "Network/Protocol/Http/Message/Response.h"

namespace ne::io
{
	class Context;
}

namespace ne::network::http::internal
{
	/**
	 * @class ParsedUrl
	 * @brief "http(s)://host[:port][/path?query]" 를 최소 분해한 결과입니다.
	 * @note 일반 URI 문법(사용자정보, 프래그먼트 등)은 다루지 않습니다. URL/연결 수립은 HTTP 버전과
	 *       무관하므로 이 유틸은 http(버전 무관) 계층의 internal 에 둡니다(HTTP/1.1·HTTP/2 공용).
	 */
	struct ParsedUrl
	{
		bool_t isSecure;
		string_t host;
		uint16_t port;
		string_t target;
	};

	[[nodiscard]] std::optional<ParsedUrl> ParseUrl(string_view_t _url);

	/**
	 * @brief 리다이렉트 Location 을 _baseUrl 기준의 절대 URL 로 해석합니다.
	 *
	 * 절대 URL("http(s)://...")은 그대로, 절대 경로("/path")는 base 의 scheme://host[:port] 에 붙이고,
	 * 상대 경로는 base target 의 디렉터리(마지막 '/' 까지)에 붙입니다. 해석 불가(빈 값, base 파싱 실패)면 nullopt.
	 */
	[[nodiscard]] std::optional<string_t> ResolveLocation(string_view_t _baseUrl, string_view_t _location);

	/** @brief _statusCode 가 자동 추적 대상 리다이렉트(301/302/303/307/308)인지 확인합니다. */
	[[nodiscard]] bool_t IsRedirect(int_t _statusCode) noexcept;

	/**
	 * @brief 리다이렉트에 맞게 재전송할 요청을 변형합니다.
	 * @note 303(그리고 관례상 301/302 의 POST)은 GET 으로 전환하며 본문과 엔티티 헤더(Content-Length/Content-Type)를
	 *       제거합니다. 307/308 은 메서드/본문을 유지합니다.
	 */
	void_t AdaptRequestForRedirect(int_t _statusCode, http::Request& _request);

	/**
	 * @brief _host:_port 로 TCP 연결하고, _isSecure 면 TLS 핸드셰이크까지 마친 IStream 을 만듭니다.
	 *
	 * 평문은 PlainStream::Connect(내부에서 DNS 후보 페일오버), TLS 는 DNS→connect 후 TlsStream::Connect
	 * 로 감쌉니다 — 프로토콜 버전과 무관한 연결 수립 진입점입니다(Client/Connection 및 향후 HTTP/2 공용).
	 */
	[[nodiscard]] ne::Task<HttpResult<std::unique_ptr<ne::network::IStream>>> EstablishStream(string_view_t _host, uint16_t _port, bool_t _isSecure, ne::io::Context& _context, std::stop_token _stopToken);

	/**
	 * @class EstablishedStream
	 * @brief EstablishStream(ALPN 판)의 결과 — 수립된 스트림과 협상된 ALPN 프로토콜입니다.
	 * @note negotiatedProtocol 은 TLS + ALPN 협상 성공 시에만 채워지고, 평문(h2c)이거나 협상 미성립이면 빈 문자열입니다.
	 */
	struct EstablishedStream
	{
		std::unique_ptr<ne::network::IStream> stream;
		string_t negotiatedProtocol;
	};

	/**
	 * @brief EstablishStream 의 ALPN 판 — TLS 경로에서 _alpnProtocols(예: {"h2"})를 제안하고
	 *        협상 결과를 함께 돌려줍니다. HTTP/2 처럼 ALPN 협상 결과가 필요한 경로에서 씁니다.
	 * @note 평문(_isSecure=false)이면 ALPN 은 무의미하므로 negotiatedProtocol 은 빈 문자열입니다(h2c 는 prior knowledge).
	 */
	[[nodiscard]] ne::Task<HttpResult<EstablishedStream>> EstablishStream(string_view_t _host, uint16_t _port, bool_t _isSecure, std::vector<string_t> _alpnProtocols, ne::io::Context& _context, std::stop_token _stopToken);
}
