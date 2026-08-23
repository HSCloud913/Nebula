 //
// Created by hscloud on 26. 7. 30.
//

#pragma once
#include <functional>
#include <stop_token>
#include <utility>
#include <vector>
#include "Base/Type.h"
#include "Base/Coroutine/Task.h"
#include "Io/Context.h"
#include "Io/Socket.h"
#include "Network/Stream/Tls/TlsStream.h"
#include "Network/Protocol/Http/Message/Method.h"
#include "Network/Protocol/Http/Message/Version.h"
#include "Network/Protocol/Http/Limits.h"
#include "Network/Protocol/Http/ServerObserver.h"
#include "Network/Protocol/Http/Message/Request.h"
#include "Network/Protocol/Http/Message/Response.h"
#include "Network/Protocol/Http/Diagnostic/Error.h"
#include "Network/Protocol/Http/Message/Params.h"

namespace ne::network::http
{
	/**
	 * @class ServerBuilder
	 * @brief 라우트를 fluent 하게 등록하고 Serve() 로 accept 루프를 구동하는 HTTP 서버 진입점입니다(HTTP/1.1·HTTP/2 공용).
	 *
	 * @code
	 *   http::ServerBuilder()
	 *       .Get("/health", [](const http::Request&) -> ne::Task<http::HttpResult<http::Response>>
	 *            { co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, "ok")); })
	 *       .Get("/users/{id}", [](const http::Request&, const http::PathParams& _params) -> ne::Task<http::HttpResult<http::Response>>
	 *            { co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, string_t(*_params.Get("id")))); })
	 *       .Serve(std::move(listener), ctx, stopToken);   // co_await
	 * @endcode
	 *
	 * 프로토콜 버전은 연결 단위로 정해집니다 — 기본(AUTO)은 TLS 경로에서 ALPN 협상 결과("h2" 면 HTTP/2,
	 * 아니면 HTTP/1.1)를 따르므로, TlsConfig::alpnProtocols 에 {"h2","http/1.1"} 을 넣으면 한 리스너에서
	 * 두 버전을 동시에 서비스합니다. 평문 경로의 AUTO 는 HTTP/1.1 이며, 평문 HTTP/2(h2c prior knowledge)는
	 * Version(HTTP_2) 로 명시해야 합니다.
	 *
	 * 경로는 세그먼트 단위로 매칭합니다(쿼리스트링 제외) — 리터럴은 정확 일치, "{name}" 은 세그먼트
	 * 하나를 캡처(퍼센트 디코딩), "{*name}" 은 남은 경로 전체를 캡처(마지막 세그먼트에만). 등록 순서대로
	 * 첫 매치가 이깁니다. 경로는 맞지만 메서드가 없으면 405(Allow 헤더), 아무 경로도 안 맞으면
	 * NotFound(미설정 시 404)로 응답합니다. 각 연결은 독립 태스크로 동시 처리됩니다.
	 */
	class ServerBuilder
	{
	public:
		using Handler = std::function<ne::Task<http::HttpResult<http::Response>>(const http::Request&)>;
		using RouteHandler = std::function<ne::Task<http::HttpResult<http::Response>>(const http::Request&, const http::PathParams&)>;

	private:
		struct Entry
		{
			http::Method method;
			string_t path;
			RouteHandler handler;
		};

		std::vector<Entry> routes;
		Handler notFound;
		const TlsConfig* tlsConfig{ nullptr };
		http::Version version{ http::Version::AUTO };
		http::Limits limits;
		ServerObserver observer;

	public:
		/** @brief _method + _path 패턴 조합에 경로 파라미터를 받는 핸들러를 등록합니다(체이닝 가능). */
		ServerBuilder& Route(const http::Method _method, const string_view_t _path, RouteHandler _handler)
		{
			routes.push_back(Entry{ _method, string_t(_path), std::move(_handler) });
			return *this;
		}

		/** @brief _method + _path 패턴 조합에 핸들러를 등록합니다(체이닝 가능) — 경로 파라미터가 필요 없는 형태. */
		ServerBuilder& Route(const http::Method _method, const string_view_t _path, Handler _handler)
		{
			return Route(_method, _path, RouteHandler{ [handler = std::move(_handler)](const http::Request& _request, const http::PathParams&) { return handler(_request); } });
		}

		ServerBuilder& Get(const string_view_t _path, Handler _handler) { return Route(http::Method::GET, _path, std::move(_handler)); }
		ServerBuilder& Post(const string_view_t _path, Handler _handler) { return Route(http::Method::POST, _path, std::move(_handler)); }
		ServerBuilder& Put(const string_view_t _path, Handler _handler) { return Route(http::Method::PUT, _path, std::move(_handler)); }
		ServerBuilder& Delete(const string_view_t _path, Handler _handler) { return Route(http::Method::DELETE_, _path, std::move(_handler)); }
		ServerBuilder& Head(const string_view_t _path, Handler _handler) { return Route(http::Method::HEAD, _path, std::move(_handler)); }
		ServerBuilder& Options(const string_view_t _path, Handler _handler) { return Route(http::Method::OPTIONS, _path, std::move(_handler)); }
		ServerBuilder& Patch(const string_view_t _path, Handler _handler) { return Route(http::Method::PATCH, _path, std::move(_handler)); }

		ServerBuilder& Get(const string_view_t _path, RouteHandler _handler) { return Route(http::Method::GET, _path, std::move(_handler)); }
		ServerBuilder& Post(const string_view_t _path, RouteHandler _handler) { return Route(http::Method::POST, _path, std::move(_handler)); }
		ServerBuilder& Put(const string_view_t _path, RouteHandler _handler) { return Route(http::Method::PUT, _path, std::move(_handler)); }
		ServerBuilder& Delete(const string_view_t _path, RouteHandler _handler) { return Route(http::Method::DELETE_, _path, std::move(_handler)); }
		ServerBuilder& Head(const string_view_t _path, RouteHandler _handler) { return Route(http::Method::HEAD, _path, std::move(_handler)); }
		ServerBuilder& Options(const string_view_t _path, RouteHandler _handler) { return Route(http::Method::OPTIONS, _path, std::move(_handler)); }
		ServerBuilder& Patch(const string_view_t _path, RouteHandler _handler) { return Route(http::Method::PATCH, _path, std::move(_handler)); }

		/** @brief 어떤 라우트에도 안 맞을 때 호출할 핸들러(미설정 시 기본 404). */
		ServerBuilder& NotFound(Handler _handler)
		{
			notFound = std::move(_handler);
			return *this;
		}

		/**
		 * @brief TLS 를 켭니다(설정 수명은 호출자가 보장). 미설정 시 평문.
		 * @note HTTP/2 를 ALPN 으로 협상하려면 _config->alpnProtocols 에 "h2" 를 넣어야 합니다(예: {"h2","http/1.1"}).
		 */
		ServerBuilder& Tls(const TlsConfig* _config)
		{
			tlsConfig = _config;
			return *this;
		}

		/**
		 * @brief 프로토콜 버전을 고정합니다. 기본 AUTO — TLS 는 ALPN 결과("h2"/그 외)로 연결마다 분기, 평문은 HTTP/1.1.
		 * @note 평문 HTTP/2(h2c) 는 클라이언트가 preface 를 바로 보내는 prior knowledge 전제이므로 HTTP_2 명시가 필요합니다.
		 */
		ServerBuilder& Version(const http::Version _version)
		{
			version = _version;
			return *this;
		}

		/**
		 * @brief 관측 훅을 등록합니다 — 액세스 로그·에러 추적(기본: 없음, 비용 0).
		 * @note 콜백은 이벤트 루프 스레드에서 동기 호출되므로 블로킹 작업을 직접 하지 마세요(ServerObserver 참고).
		 */
		ServerBuilder& Observe(ServerObserver _observer)
		{
			observer = std::move(_observer);
			return *this;
		}

		/**
		 * @brief 요청 헤더 블록 크기 상한(기본 8KB).
		 * @note 초과 시 HTTP/1.1 은 431 응답 후 연결 종료, HTTP/2 는 연결 종료(HPACK 폭탄 대비).
		 */
		ServerBuilder& MaxHeaderBytes(const std::size_t _bytes)
		{
			limits.maxHeaderBytes = _bytes;
			return *this;
		}

		/**
		 * @brief 요청 본문 크기 상한(기본 64MB).
		 * @note 초과 시 HTTP/1.1 은 413 응답 후 연결 종료, HTTP/2 는 해당 스트림만 RST_STREAM(연결 유지).
		 */
		ServerBuilder& MaxBodyBytes(const std::size_t _bytes)
		{
			limits.maxBodyBytes = _bytes;
			return *this;
		}

		/**
		 * @brief _listener 에서 연결을 계속 accept 하며 각 연결을 등록된 라우트로 동시 처리합니다.
		 * @note 연결마다 TLS 핸드셰이크(설정 시) 후 프로토콜 버전을 정해 HTTP/1.1(keep-alive)·HTTP/2(멀티플렉싱)
		 *       엔진으로 위임합니다. _stopToken 취소 또는 Accept 실패 시 진행 중인 연결을 모두 취소·완료시킨 뒤
		 *       종료합니다. 리스너는 값으로 받아 내부에서 소유합니다.
		 */
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> Serve(ne::io::Socket _listener, ne::io::Context& _context, std::stop_token _stopToken = {}) const;

	private:
		// 등록된 라우트를 디스패치하는 Handler 를 만든다(현재 라우트 스냅샷을 복사해 소유).
		[[nodiscard]] Handler BuildHandler() const;
	};
}
