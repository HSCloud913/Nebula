//
// Created by hscloud on 26. 7. 30.
//

#pragma once
#include <chrono>
#include <memory>
#include <optional>
#include <stop_token>
#include <utility>
#include "Base/Type.h"
#include "Base/Coroutine/Task.h"
#include "Io/Context.h"
#include "Io/Engine.h"
#include "Network/Protocol/Http/Message/Method.h"
#include "Network/Protocol/Http/Message/Version.h"
#include "Network/Protocol/Http/Endpoint.h"
#include "Network/Protocol/Http/Message/Request.h"
#include "Network/Protocol/Http/Message/Response.h"
#include "Network/Protocol/Http/Diagnostic/Error.h"
#include "Network/Protocol/Http/ResponseCallbacks.h"

namespace ne::io
{
	class Runtime;
}

namespace ne::network
{
	class IStream;
}

namespace ne::network::http_1::internal
{
	class MessageReader;
}

namespace ne::network::http_2::internal
{
	class ClientConnection;
}

namespace ne::network::http
{
	class ClientSession;

	/**
	 * @class ClientBuilder
	 * @brief HTTP 요청을 fluent 하게 조립해 전송하는 공개 클라이언트 진입점입니다(HTTP/1.1·HTTP/2 공용).
	 *
	 * @code
	 *   auto res = ne::network::http::Get("https://api.x/things").Header("Authorization","Bearer T").SendSync();
	 * @endcode
	 *
	 * 프로토콜 버전은 기본(AUTO)으로 https 는 ALPN 협상 결과("h2" 면 HTTP/2, 아니면 HTTP/1.1)를 따르고,
	 * 평문(http)은 HTTP/1.1 을 씁니다. Version(HTTP_2) 로 고정하면 https 는 "h2" 협상 실패 시 에러,
	 * 평문은 h2c prior knowledge 로 preface 를 바로 보냅니다.
	 *
	 * 터미널: SendSync([engine])(블로킹·자체 Runtime) / Send(ctx)(비동기·외부 Context) / Stream(sink, ctx)(스트리밍).
	 * 연결을 재사용(keep-alive/멀티플렉싱)하려면 Connect() 로 세션을 얻어 session.Send(builder) 를 반복하세요.
	 * 모든 터미널은 빌더 상태를 값으로 복사해 코루틴 프레임이 소유하므로 임시 빌더로 호출해도 안전합니다.
	 */
	class ClientBuilder
	{
		friend class ClientSession;

	public:
		ClientBuilder(const http::Method _method, const string_view_t _url)
			: url(_url) { request.method = _method; }

	private:
		string_t url;
		http::Request request;
		std::chrono::milliseconds timeout{ 0 };
		int_t maxRedirects{ 0 }; // 0 = 리다이렉트 자동 추적 안 함
		http::Version version{ http::Version::AUTO };

	public:
		ClientBuilder& Header(const string_view_t _name, const string_view_t _value);
		ClientBuilder& Body(http::Body _body);
		ClientBuilder& Body(const string_view_t _text);
		ClientBuilder& Timeout(const std::chrono::milliseconds _timeout);

		/** @brief HTTP Basic 인증을 설정합니다(Authorization: Basic base64(user:password)). */
		ClientBuilder& BasicAuth(string_view_t _user, string_view_t _password);

		/**
		 * @brief 3xx 리다이렉트를 최대 _max 회 자동 추적합니다(원샷 Send/SendSync 전용 — 세션/Stream 은 추적 안 함).
		 * @note 303(그리고 관례상 301/302 의 POST)은 GET 전환+본문 제거, 307/308 은 메서드/본문 유지.
		 *       Timeout() 은 리다이렉트 전체 체인에 걸쳐 적용됩니다.
		 */
		ClientBuilder& FollowRedirects(const int_t _max = 5);

		/** @brief 프로토콜 버전을 고정합니다. 기본 AUTO — https 는 ALPN 결과, 평문은 HTTP/1.1 (클래스 설명 참고). */
		ClientBuilder& Version(const http::Version _version);

	public:
		/** @brief 비동기 전송 — 주어진 Context(호출자의 이벤트 루프) 위에서 요청 1건을 보내고 응답을 받습니다(연결은 요청 후 닫힘). */
		[[nodiscard]] ne::Task<http::HttpResult<http::Response>> Send(ne::io::Context& _context, std::stop_token _stopToken = {}) const { return SendImpl(url, request, timeout, maxRedirects, version, _context, std::move(_stopToken)); }

		/** @brief 블로킹 전송 — 자체 Runtime 을 세워 완료까지 구동하고 결과를 값으로 반환합니다(엔진은 이 경로에서만 의미). */
		[[nodiscard]] http::HttpResult<http::Response> SendSync(ne::io::EngineType _engineType = ne::io::EngineType::PROACTOR) const;

		/** @brief 스트리밍 전송 — 본문을 _sink 로 조각째 받습니다(전체 버퍼링 없음). */
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> Stream(http::ResponseCallbacks _sink, ne::io::Context& _context, std::stop_token _stopToken = {}) const { return StreamImpl(url, request, std::move(_sink), timeout, version, _context, std::move(_stopToken)); }

	private:
		// 소유 데이터를 값 파라미터로 받아 코루틴 프레임이 소유 — 임시 빌더가 먼저 파괴돼도 안전.
		[[nodiscard]] static ne::Task<http::HttpResult<http::Response>> SendImpl(string_t _url, http::Request _request, std::chrono::milliseconds _timeout, int_t _maxRedirects, http::Version _version, ne::io::Context& _context, std::stop_token _stopToken);
		[[nodiscard]] static ne::Task<http::HttpResult<void_t>> StreamImpl(string_t _url, http::Request _request, http::ResponseCallbacks _sink, std::chrono::milliseconds _timeout, http::Version _version, ne::io::Context& _context, std::stop_token _stopToken);
	};

	/**
	 * @class ClientSession
	 * @brief 연결(비동기)을 유지하며 여러 요청을 재사용합니다 — 호출자의 Context(이벤트 루프)에 얹혀 돕니다.
	 *
	 * 첫 Send 에서 버전 정책에 따라 연결을 수립합니다 — HTTP/1.1 은 keep-alive 로 요청을 순차 재사용하고,
	 * HTTP/2 는 하나의 연결에서 여러 Send 를 스트림으로 멀티플렉싱합니다(호출자가 여러 Send Task 를 함께
	 * 구동할 경우 동시 진행). Send(builder) 의 url 은 요청 target(경로)로 해석되며, 세션이 host/port/TLS 를
	 * 결정합니다.
	 *
	 * @note HTTP/1.1 경로: 응답/요청이 Connection: close 면 닫고 다음 Send 에서 다시 열며, 재사용 연결이
	 *       죽어 있으면 멱등 메서드에 한해 1회 재연결 후 재전송합니다.
	 * @note HTTP/2 경로: 파괴/Close() 시 연결 정리는 이벤트 루프에 위임됩니다(in-flight I/O 취소는 즉시,
	 *       파괴는 드라이버 종료 후). Context/엔진 자체를 곧 파괴할 거라면 그 전에 Shutdown() 을 co_await
	 *       해 정리가 "완료"됐음을 보장하세요.
	 */
	class ClientSession
	{
	public:
		ClientSession(http::Endpoint _endpoint, ne::io::Context& _context, http::Version _version = http::Version::AUTO) noexcept;
		~ClientSession();

		NEBULA_NON_COPYABLE(ClientSession)
		ClientSession(ClientSession&&) noexcept;
		ClientSession& operator=(ClientSession&&) noexcept;

	private:
		http::Endpoint endpoint;
		ne::io::Context* context;
		http::Version version;
		std::unique_ptr<ne::network::IStream> stream;                    // HTTP/1.1 연결(미연결이면 nullptr)
		std::unique_ptr<http_1::internal::MessageReader> reader;         // stream 위에서 메시지 경계를 이어 읽는다
		std::unique_ptr<http_2::internal::ClientConnection> connection;  // HTTP/2 연결(미연결이면 nullptr)

	public:
		/** @brief 연결을 재사용해 _request(빌더)를 보내고 응답을 받습니다(빌더는 값으로 소유 — 임시로 넘겨도 안전). */
		[[nodiscard]] ne::Task<http::HttpResult<http::Response>> Send(ClientBuilder _request, std::stop_token _stopToken = {});

		[[nodiscard]] bool_t IsOpen() const noexcept;

		/** @brief 연결을 닫습니다 — HTTP/2 는 in-flight I/O 를 즉시 취소하고 실제 파괴를 루프에 위임합니다. */
		void_t Close();

		/**
		 * @brief 연결을 정상 종료합니다 — HTTP/2 백그라운드 드라이버의 진행 중 I/O 를 취소하고 완전히 끝날 때까지 기다립니다.
		 * @note 정리 "완료"가 필요할 때 씁니다(예: 이 Context/엔진을 곧 파괴할 때). HTTP/1.1 연결은 즉시 닫힙니다.
		 *       이 세션의 Context 를 구동하는 루프 위에서 co_await 해야 하며, 반환 직후 세션을 파괴해도 안전합니다.
		 */
		[[nodiscard]] ne::Task<void_t> Shutdown();

		[[nodiscard]] const http::Endpoint& RemoteEndpoint() const noexcept { return endpoint; }

	private:
		// 버전 정책에 따라 연결을 수립한다(이미 열려 있으면 no-op). AUTO+TLS 는 ALPN 결과로 h1/h2 를 정한다.
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> EnsureConnected(std::stop_token _stopToken);

		// 수립된 연결로 요청 1건을 보낸다(h1 keep-alive / h2 멀티플렉싱 분기).
		[[nodiscard]] ne::Task<http::HttpResult<http::Response>> SendCore(http::Request _request, std::stop_token _stopToken);
		[[nodiscard]] ne::Task<http::HttpResult<http::Response>> SendCoreHttp1(http::Request _request, std::stop_token _stopToken);
	};

	/**
	 * @class SyncClientSession
	 * @brief 연결(블로킹)을 유지하며 여러 요청을 재사용합니다 — 자체 Runtime 을 소유해 외부 Context 가 필요 없습니다.
	 *
	 * 스크립트·도구처럼 이벤트 루프 없이 여러 요청을 한 서버로 보낼 때 씁니다. Send(builder) 는 내부
	 * Runtime 으로 완료까지 블로킹 구동해 결과를 값으로 돌려줍니다(SendSync 의 keep-alive 판).
	 */
	class SyncClientSession
	{
	public:
		explicit SyncClientSession(http::Endpoint _endpoint, ne::io::EngineType _engineType = ne::io::EngineType::PROACTOR, http::Version _version = http::Version::AUTO);
		~SyncClientSession();

		NEBULA_NON_COPYABLE(SyncClientSession)
		SyncClientSession(SyncClientSession&&) noexcept;
		SyncClientSession& operator=(SyncClientSession&&) noexcept;

	private:
		std::unique_ptr<ne::io::Runtime> runtime;     // engine+timer+context 소유
		std::unique_ptr<ClientSession> session;       // runtime 의 Context 위에서 도는 실제 세션

	public:
		/** @brief 연결을 재사용해 _request 를 블로킹으로 보내고 응답을 값으로 반환합니다. */
		[[nodiscard]] http::HttpResult<http::Response> Send(ClientBuilder _request);

		[[nodiscard]] bool_t IsOpen() const noexcept;
		void_t Close();
	};

	[[nodiscard]] inline ClientBuilder Get(const string_view_t _url) { return ClientBuilder(http::Method::GET, _url); }
	[[nodiscard]] inline ClientBuilder Head(const string_view_t _url) { return ClientBuilder(http::Method::HEAD, _url); }
	[[nodiscard]] inline ClientBuilder Delete(const string_view_t _url) { return ClientBuilder(http::Method::DELETE_, _url); }
	[[nodiscard]] inline ClientBuilder Options(const string_view_t _url) { return ClientBuilder(http::Method::OPTIONS, _url); }
	[[nodiscard]] inline ClientBuilder Post(const string_view_t _url) { return ClientBuilder(http::Method::POST, _url); }
	[[nodiscard]] inline ClientBuilder Put(const string_view_t _url) { return ClientBuilder(http::Method::PUT, _url); }
	[[nodiscard]] inline ClientBuilder Patch(const string_view_t _url) { return ClientBuilder(http::Method::PATCH, _url); }

	/** @brief "http(s)://host[:port]" → 비동기 세션(외부 Context 에 얹힘). 파싱 실패 시 nullopt. */
	[[nodiscard]] std::optional<ClientSession> Connect(string_view_t _url, ne::io::Context& _context, http::Version _version = http::Version::AUTO);
	/** @brief Endpoint → 비동기 세션(외부 Context 에 얹힘). */
	[[nodiscard]] inline ClientSession Connect(http::Endpoint _endpoint, ne::io::Context& _context, http::Version _version = http::Version::AUTO) { return ClientSession(std::move(_endpoint), _context, _version); }

	/** @brief "http(s)://host[:port]" → 블로킹 세션(자체 Runtime, Context 불필요). 파싱 실패 시 nullopt. */
	[[nodiscard]] std::optional<SyncClientSession> ConnectSync(string_view_t _url, ne::io::EngineType _engineType = ne::io::EngineType::PROACTOR, http::Version _version = http::Version::AUTO);
	/** @brief Endpoint → 블로킹 세션(자체 Runtime, Context 불필요). */
	[[nodiscard]] inline SyncClientSession ConnectSync(http::Endpoint _endpoint, ne::io::EngineType _engineType = ne::io::EngineType::PROACTOR, http::Version _version = http::Version::AUTO) { return SyncClientSession(std::move(_endpoint), _engineType, _version); }
}
