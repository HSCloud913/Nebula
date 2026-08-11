//
// Created by hscloud on 26. 7. 20.
//

#pragma once
#include <memory>
#include <stop_token>
#include "Base/Coroutine/Task.h"
#include "Io/Context.h"
#include "Network/Stream/IStream.h"
#include "Network/Protocol/Http/Endpoint.h"
#include "Network/Protocol/Http/Message/Request.h"
#include "Network/Protocol/Http/Message/Response.h"
#include "Network/Protocol/Http/Diagnostic/Error.h"
#include "Network/Protocol/Http/ResponseCallbacks.h"

namespace ne::network::http_1::internal
{
	/**
	 * @class Client
	 * @brief HTTP/1.1 요청을 주어진 Context 위에서 수행하는 저수준 내부 엔진입니다(공개 API 아님).
	 *
	 * 공개 진입점은 자유 함수 Get/Post/... 와 ClientBuilder 이며, 그 조립 결과를 이 Client 가 실제로
	 * 연결·송신·수신합니다. 요청 1회당 연결을 새로 맺고 응답 수신 후 닫습니다(연결 재사용은 ClientSession).
	 * URL의 scheme 이 https 이면 TlsStream, http 이면 PlainStream 위에서 동작합니다.
	 */
	class Client
	{
	public:
		/** @brief _endpoint 로 직접 연결해 _request 를 보내고 응답을 받습니다(URL 파싱 없는 저수준 진입점). */
		[[nodiscard]] static ne::Task<http::HttpResult<http::Response>> Request(const http::Endpoint& _endpoint, http::Request _request, ne::io::Context& _context, std::stop_token _stopToken = {});

		/** @brief _url 을 파싱해 Endpoint 를 얻은 뒤 위 오버로드에 위임합니다. */
		[[nodiscard]] static ne::Task<http::HttpResult<http::Response>> Request(string_view_t _url, http::Request _request, ne::io::Context& _context, std::stop_token _stopToken = {});

		/** @brief Request 의 스트리밍 판 — 응답 본문을 _sink 로 조각째 흘려 받습니다(전체 버퍼링 없음). */
		[[nodiscard]] static ne::Task<http::HttpResult<void_t>> Stream(const http::Endpoint& _endpoint, http::Request _request, http::ResponseCallbacks _sink, ne::io::Context& _context, std::stop_token _stopToken = {});
		[[nodiscard]] static ne::Task<http::HttpResult<void_t>> Stream(string_view_t _url, http::Request _request, http::ResponseCallbacks _sink, ne::io::Context& _context, std::stop_token _stopToken = {});

		/**
		 * @brief 이미 수립된 스트림(평문 또는 TLS 핸드셰이크 완료) 위에서 요청 1개를 보내고 응답을 받습니다(1회성 — 완료 후 스트림을 닫음).
		 * @note ALPN 협상 결과로 버전을 고른 뒤 그 스트림을 재사용하는 통합 클라이언트용 진입점 — _endpoint 는 Host 헤더 기본값에만 쓰입니다.
		 */
		[[nodiscard]] static ne::Task<http::HttpResult<http::Response>> RequestOver(std::unique_ptr<IStream> _stream, const http::Endpoint& _endpoint, http::Request _request, std::stop_token _stopToken = {});

		/** @brief RequestOver 의 스트리밍 판. */
		[[nodiscard]] static ne::Task<http::HttpResult<void_t>> StreamOver(std::unique_ptr<IStream> _stream, const http::Endpoint& _endpoint, http::Request _request, http::ResponseCallbacks _sink, std::stop_token _stopToken = {});
	};
}
