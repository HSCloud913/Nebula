//
// Created by hscloud on 26. 7. 30.
//

#include "Network/Protocol/Http/ClientBuilder.h"

#include <string>
#include <utility>
#include <vector>
#include "Base/Coroutine/Detached.h"
#include "Io/Runtime.h"
#include "Network/Stream/IStream.h"
#include "Network/Protocol/Http/Internal/Http1/Client.h"
#include "Network/Protocol/Http/Internal/Http1/Parser.h"
#include "Network/Protocol/Http/Internal/Http2/Connection.h"
#include "Network/Protocol/Http/Internal/Transport.h"
#include "Network/Protocol/Http/Internal/Timeout.h"
#include "Util/Base64.h"
#include "Util/StringFormat.h"



namespace ne::network::http
{
	namespace
	{
		// 재시도해도 안전한(부작용이 멱등한) 메서드인지. POST/PATCH/CONNECT 는 재전송하면 안 된다.
		bool_t IsIdempotent(const Method _method) noexcept
		{
			switch (_method)
			{
				case Method::GET:
				case Method::HEAD:
				case Method::PUT:
				case Method::DELETE_:
				case Method::OPTIONS:
				case Method::TRACE:
					return true;
				default:
					return false;
			}
		}

		bool_t WantsClose(const Headers& _headers) noexcept
		{
			const auto connection = _headers.Get("Connection");
			return connection && ne::util::StringFormat::EqualCaseInsensitive(string_view_t(*connection), string_view_t("close"));
		}

		string_t MakeAuthority(const Endpoint& _endpoint)
		{
			const uint16_t defaultPort = _endpoint.isSecure ? 443 : 80;
			if (_endpoint.port == defaultPort) return _endpoint.host;
			return _endpoint.host + ":" + std::to_string(_endpoint.port);
		}

		// 세션이 명시적 Shutdown 없이 버려질 때 연결 정리를 이벤트 루프에 위임하는 fire-and-forget 리퍼.
		// DrainClose 의 동기 구간(stop 요청 + 스트림 Close)은 이 자리에서 즉시 실행되어 in-flight I/O 를
		// 취소하고, 드라이버 종료를 기다린 뒤 connection 이 이 프레임과 함께 파괴된다. 루프가 더 이상
		// 돌지 않으면 프레임이 남는다(누수 — 파괴된 엔진으로 완료가 배달되는 크래시보다 안전한 쪽).
		ne::Detached ReapConnection(std::unique_ptr<http_2::internal::ClientConnection> _connection)
		{
			co_await _connection->DrainClose();
		}

		// 버전 정책에 따른 ALPN 후보. 평문이면 빈 목록(HTTP/2 는 h2c prior knowledge).
		std::vector<string_t> AlpnCandidates(const Version _version, const bool_t _isSecure)
		{
			std::vector<string_t> alpn;
			if (!_isSecure) return alpn;

			alpn.push_back("h2");
			if (_version == Version::AUTO) alpn.push_back("http/1.1");
			return alpn;
		}

		// 이미 수립된 h2 스트림 위에 ClientConnection 을 만들고 preface/SETTINGS 를 교환한다.
		ne::Task<HttpResult<std::unique_ptr<http_2::internal::ClientConnection>>> StartHttp2(std::unique_ptr<ne::network::IStream> _stream, const Endpoint& _endpoint, ne::io::Context& _context, std::stop_token _stopToken)
		{
			using R = HttpResult<std::unique_ptr<http_2::internal::ClientConnection>>;

			auto connection = std::make_unique<http_2::internal::ClientConnection>(std::move(_stream), _context, MakeAuthority(_endpoint), _endpoint.isSecure);

			if (auto started = co_await connection->Start(std::move(_stopToken)); started.IsError()) co_return R::Error(std::move(started.Error()));

			co_return R::Ok(std::move(connection));
		}

		// 한 홉(1회성) 전송 — 버전 정책에 따라 h1/h2 를 골라 요청 1건을 보낸다. AUTO+TLS 는 ALPN 협상
		// 결과로 분기하며, 어느 쪽이든 수립한 스트림을 그대로 쓴다(재연결 없음).
		ne::Task<HttpResult<Response>> SendOnce(Endpoint _endpoint, Request _request, const Version _version, ne::io::Context& _context, std::stop_token _stopToken)
		{
			using R = HttpResult<Response>;

			// HTTP/1.1 로 확정된 경로(강제 또는 평문 AUTO)는 h1 엔진이 연결 수립까지 처리한다.
			if (_version == Version::HTTP_1_1 || (_version == Version::AUTO && !_endpoint.isSecure)) co_return co_await http_1::internal::Client::Request(_endpoint, std::move(_request), _context, std::move(_stopToken));

			auto established = co_await internal::EstablishStream(_endpoint.host, _endpoint.port, _endpoint.isSecure, AlpnCandidates(_version, _endpoint.isSecure), _context, _stopToken);
			if (established.IsError()) co_return R::Error(std::move(established.Error()));

			if (_version == Version::HTTP_2 && _endpoint.isSecure && established.Value().negotiatedProtocol != "h2") co_return R::Error(HttpError(HttpErrorKind::UNSUPPORTED_VERSION, "server did not negotiate HTTP/2 (ALPN h2)").Context("[Client/Establish]"));

			// AUTO+TLS 에서 h2 미협상 → 같은 스트림 위에서 HTTP/1.1 로 보낸다.
			const bool_t useHttp2 = !_endpoint.isSecure || established.Value().negotiatedProtocol == "h2";
			if (!useHttp2) co_return co_await http_1::internal::Client::RequestOver(std::move(established.Value().stream), _endpoint, std::move(_request), std::move(_stopToken));

			auto connResult = co_await StartHttp2(std::move(established.Value().stream), _endpoint, _context, _stopToken);
			if (connResult.IsError()) co_return R::Error(std::move(connResult.Error()));

			std::unique_ptr<http_2::internal::ClientConnection> connection = std::move(connResult.Value());
			R result = co_await connection->Send(std::move(_request), _stopToken);

			// 정상 종료 — 드라이버의 in-flight I/O 를 취소하고 완전히 끝난 뒤 반환한다(반환 직후 connection 파괴 안전).
			co_await connection->DrainClose();
			co_return result;
		}

		// 리다이렉트 자동 추적 루프 — 매 홉을 1회성 SendOnce 로 보내고, 3xx + Location 이면 절대 URL 로
		// 해석해 다음 홉으로 넘어간다. 한도 도달·Location 부재·해석 불가·비리다이렉트면 그 응답을 그대로 반환한다.
		ne::Task<HttpResult<Response>> SendFollowingRedirects(string_t _url, Request _request, const int_t _maxRedirects, const Version _version, ne::io::Context& _context, std::stop_token _stopToken)
		{
			using R = HttpResult<Response>;

			for (int_t hop = 0; ; ++hop)
			{
				const auto parsed = internal::ParseUrl(_url);
				if (!parsed) co_return R::Error(HttpError(HttpErrorKind::MALFORMED_MESSAGE, "invalid URL").Context("[Client/Send]"));

				Request hopRequest = _request; // 다음 홉을 위해 원본은 남겨 둔다(AdaptRequestForRedirect 가 변형)
				hopRequest.target = parsed->target;

				auto response = co_await SendOnce(Endpoint{ parsed->host, parsed->port, parsed->isSecure }, std::move(hopRequest), _version, _context, _stopToken);
				if (response.IsError()) co_return std::move(response);

				const int_t status = response.Value().statusCode;
				if (hop >= _maxRedirects || !internal::IsRedirect(status)) co_return std::move(response);

				const auto location = response.Value().headers.Get("Location");
				if (!location) co_return std::move(response);

				auto resolved = internal::ResolveLocation(_url, *location);
				if (!resolved) co_return std::move(response);

				internal::AdaptRequestForRedirect(status, _request);
				_url = std::move(*resolved);
			}
		}

		// SendOnce 의 스트리밍 판(리다이렉트 추적 없음). _sink 는 호출자(StreamImpl 프레임)가 소유한다.
		ne::Task<HttpResult<void_t>> StreamOnce(Endpoint _endpoint, Request _request, const ResponseCallbacks& _sink, const Version _version, ne::io::Context& _context, std::stop_token _stopToken)
		{
			using R = HttpResult<void_t>;

			if (_version == Version::HTTP_1_1 || (_version == Version::AUTO && !_endpoint.isSecure)) co_return co_await http_1::internal::Client::Stream(_endpoint, std::move(_request), _sink, _context, std::move(_stopToken));

			auto established = co_await internal::EstablishStream(_endpoint.host, _endpoint.port, _endpoint.isSecure, AlpnCandidates(_version, _endpoint.isSecure), _context, _stopToken);
			if (established.IsError()) co_return R::Error(std::move(established.Error()));

			if (_version == Version::HTTP_2 && _endpoint.isSecure && established.Value().negotiatedProtocol != "h2") co_return R::Error(HttpError(HttpErrorKind::UNSUPPORTED_VERSION, "server did not negotiate HTTP/2 (ALPN h2)").Context("[Client/Establish]"));

			const bool_t useHttp2 = !_endpoint.isSecure || established.Value().negotiatedProtocol == "h2";
			if (!useHttp2) co_return co_await http_1::internal::Client::StreamOver(std::move(established.Value().stream), _endpoint, std::move(_request), _sink, std::move(_stopToken));

			auto connResult = co_await StartHttp2(std::move(established.Value().stream), _endpoint, _context, _stopToken);
			if (connResult.IsError()) co_return R::Error(std::move(connResult.Error()));

			std::unique_ptr<http_2::internal::ClientConnection> connection = std::move(connResult.Value());
			R result = co_await connection->SendStreaming(std::move(_request), _sink, _stopToken);

			co_await connection->DrainClose();
			co_return result;
		}
	}



	ClientBuilder& ClientBuilder::Header(const string_view_t _name, const string_view_t _value) {
		request.headers.Add(_name, _value);
		return *this;
	}

	ClientBuilder& ClientBuilder::WithHeaders(http::Headers _headers) {
		request.headers = std::move(_headers);
		return *this;
	}

	ClientBuilder& ClientBuilder::Body(http::Body _body) {
		request.body = std::move(_body);
		return *this;
	}

	ClientBuilder& ClientBuilder::Body(const string_view_t _text) {
		request.body = http::Body::FromString(_text);
		return *this;
	}

	ClientBuilder& ClientBuilder::Timeout(const std::chrono::milliseconds _timeout) {
		timeout = _timeout;
		return *this;
	}

	ClientBuilder& ClientBuilder::BasicAuth(const string_view_t _user, const string_view_t _password)
	{
		string_t credentials;
		credentials.reserve(_user.size() + 1 + _password.size());
		credentials.append(_user).append(1, ':').append(_password);

		request.headers.Set("Authorization", "Basic " + ne::util::Base64::Encode(credentials));
		return *this;
	}

	ClientBuilder& ClientBuilder::FollowRedirects(const int_t _max) {
		maxRedirects = _max;
		return *this;
	}

	ClientBuilder& ClientBuilder::Version(const http::Version _version) {
		version = _version;
		return *this;
	}



	ne::Task<HttpResult<Response>> ClientBuilder::SendImpl(string_t _url, Request _request, std::chrono::milliseconds _timeout, int_t _maxRedirects, http::Version _version, ne::io::Context& _context, std::stop_token _stopToken)
	{
		// Timeout() 은 연결 수립부터 리다이렉트 전체 체인까지 걸쳐 적용된다(FollowRedirects 문서 계약).
		if (_timeout > std::chrono::milliseconds::zero()) co_return co_await internal::WithTimeout(_context, _timeout, [&](std::stop_token _token) { return SendFollowingRedirects(_url, _request, _maxRedirects, _version, _context, std::move(_token)); });

		co_return co_await SendFollowingRedirects(std::move(_url), std::move(_request), _maxRedirects, _version, _context, std::move(_stopToken));
	}

	ne::Task<HttpResult<void_t>> ClientBuilder::StreamImpl(string_t _url, Request _request, ResponseCallbacks _sink, std::chrono::milliseconds _timeout, http::Version _version, ne::io::Context& _context, std::stop_token _stopToken)
	{
		using R = HttpResult<void_t>;

		const auto parsed = internal::ParseUrl(_url);
		if (!parsed) co_return R::Error(HttpError(HttpErrorKind::MALFORMED_MESSAGE, "invalid URL").Context("[Client/Stream]"));

		_request.target = parsed->target;
		const Endpoint endpoint{ parsed->host, parsed->port, parsed->isSecure };

		if (_timeout > std::chrono::milliseconds::zero()) co_return co_await internal::WithTimeout(_context, _timeout, [&](std::stop_token _token) { return StreamOnce(endpoint, _request, _sink, _version, _context, std::move(_token)); });

		co_return co_await StreamOnce(endpoint, std::move(_request), _sink, _version, _context, std::move(_stopToken));
	}

	HttpResult<Response> ClientBuilder::SendSync(const ne::io::EngineType _engineType) const
	{
		ne::io::Runtime runtime(_engineType);
		if (!runtime.IsValid()) return HttpResult<Response>::Error(HttpError{ HttpErrorKind::TRANSPORT, "I/O runtime initialization failed" });

		return runtime.BlockOn(SendImpl(url, request, timeout, maxRedirects, version, runtime.GetContext(), std::stop_token{}));
	}



	/*--------------------------------------------------*/



	ClientSession::ClientSession(Endpoint _endpoint, ne::io::Context& _context, const http::Version _version) noexcept
		: endpoint(std::move(_endpoint))
		, context(&_context)
		, version(_version) {}

	ClientSession::~ClientSession() { Close(); }
	ClientSession::ClientSession(ClientSession&&) noexcept = default;

	ClientSession& ClientSession::operator=(ClientSession&& _other) noexcept
	{
		if (this != &_other)
		{
			Close(); // 덮어써 버려지는 기존 연결도 정리(h2 는 리퍼로 위임)
			endpoint = std::move(_other.endpoint);
			context = _other.context;
			version = _other.version;
			stream = std::move(_other.stream);
			reader = std::move(_other.reader);
			connection = std::move(_other.connection);
		}
		return *this;
	}

	bool_t ClientSession::IsOpen() const noexcept
	{
		if (connection) return connection->IsOpen();
		return stream != nullptr && stream->IsOpen();
	}

	void_t ClientSession::Close()
	{
		if (stream) (void_t)stream->Close();
		reader.reset();
		stream.reset();

		// h2 연결 정리는 리퍼에 위임한다 — in-flight I/O 취소는 이 자리에서 즉시 일어나고, 드라이버가
		// 완전히 끝난 다음 tick 에 connection 이 파괴된다(루프가 도는 한 in-flight op 를 문 채 파괴되지 않음).
		if (connection) ReapConnection(std::move(connection));
	}

	ne::Task<void_t> ClientSession::Shutdown()
	{
		if (stream) (void_t)stream->Close();
		reader.reset();
		stream.reset();

		// driverDone 는 지연 재개(SignalDeferred)라 이 코루틴은 드라이버 프레임이 완전히 물러난 뒤 깨어난다
		// — 반환 직후 세션/connection 을 파괴해도 안전하다.
		if (connection) co_await connection->DrainClose();
		connection.reset();
		co_return;
	}

	ne::Task<HttpResult<void_t>> ClientSession::EnsureConnected(std::stop_token _stopToken)
	{
		using R = HttpResult<void_t>;

		if (IsOpen()) co_return R::Ok();

		Close(); // 죽은 연결의 잔재(스트림/리더/h2 커넥션)를 정리하고 새로 수립한다

		// HTTP/1.1 로 확정된 경로(강제 또는 평문 AUTO)는 비-ALPN 수립.
		if (version == http::Version::HTTP_1_1 || (version == http::Version::AUTO && !endpoint.isSecure))
		{
			auto established = co_await internal::EstablishStream(endpoint.host, endpoint.port, endpoint.isSecure, *context, _stopToken);
			if (established.IsError()) co_return R::Error(std::move(established.Error()));

			stream = std::move(established.Value());
			reader = std::make_unique<http_1::internal::MessageReader>(*stream);
			co_return R::Ok();
		}

		auto established = co_await internal::EstablishStream(endpoint.host, endpoint.port, endpoint.isSecure, AlpnCandidates(version, endpoint.isSecure), *context, _stopToken);
		if (established.IsError()) co_return R::Error(std::move(established.Error()));

		if (version == http::Version::HTTP_2 && endpoint.isSecure && established.Value().negotiatedProtocol != "h2") co_return R::Error(HttpError(HttpErrorKind::UNSUPPORTED_VERSION, "server did not negotiate HTTP/2 (ALPN h2)").Context("[ClientSession/Establish]"));

		// AUTO+TLS 에서 h2 미협상 → 같은 스트림을 HTTP/1.1 keep-alive 연결로 쓴다.
		const bool_t useHttp2 = !endpoint.isSecure || established.Value().negotiatedProtocol == "h2";
		if (!useHttp2)
		{
			stream = std::move(established.Value().stream);
			reader = std::make_unique<http_1::internal::MessageReader>(*stream);
			co_return R::Ok();
		}

		auto connResult = co_await StartHttp2(std::move(established.Value().stream), endpoint, *context, std::move(_stopToken));
		if (connResult.IsError()) co_return R::Error(std::move(connResult.Error()));

		connection = std::move(connResult.Value());
		co_return R::Ok();
	}

	ne::Task<HttpResult<Response>> ClientSession::Send(ClientBuilder _request, std::stop_token _stopToken)
	{
		using R = HttpResult<Response>;

		if (auto ensured = co_await EnsureConnected(_stopToken); ensured.IsError()) co_return R::Error(std::move(ensured.Error()));

		// 빌더의 url 은 요청 target(경로)로 해석한다. 세션이 host/port/TLS 를 결정한다.
		Request request;
		request.method = _request.request.method;
		request.target = std::move(_request.url);
		request.headers = std::move(_request.request.headers);
		request.body = std::move(_request.request.body);

		const std::chrono::milliseconds timeout = _request.timeout;
		if (timeout > std::chrono::milliseconds::zero()) co_return co_await internal::WithTimeout(*context, timeout, [this, request](std::stop_token _token) mutable { return SendCore(std::move(request), std::move(_token)); });

		co_return co_await SendCore(std::move(request), std::move(_stopToken));
	}

	ne::Task<HttpResult<Response>> ClientSession::SendCore(Request _request, std::stop_token _stopToken)
	{
		if (connection) co_return co_await connection->Send(std::move(_request), std::move(_stopToken));

		co_return co_await SendCoreHttp1(std::move(_request), std::move(_stopToken));
	}

	ne::Task<HttpResult<Response>> ClientSession::SendCoreHttp1(Request _request, std::stop_token _stopToken)
	{
		using R = HttpResult<Response>;

		// 스트리밍 본문(BodyProducer)은 서버 응답 전용 — 조용히 Content-Length: 0 으로 나가는 것을 막는다.
		if (_request.body.IsStreaming()) co_return R::Error(HttpError(HttpErrorKind::MALFORMED_MESSAGE, "streaming request body is not supported").Context("[ClientSession/Send]"));

		if (!_request.headers.Has("Host")) _request.headers.Set("Host", endpoint.host);
		if (!_request.body.IsEmpty() && !_request.headers.Has("Content-Length")) _request.headers.Set("Content-Length", std::to_string(_request.body.Size()));
		if (!_request.headers.Has("Connection")) _request.headers.Set("Connection", "keep-alive");

		const bool_t idempotent = IsIdempotent(_request.method);

		auto builtHead = http_1::internal::BuildRequestHead(_request);
		if (builtHead.IsError()) co_return R::Error(std::move(builtHead.Error()));
		const string_t head = std::move(builtHead.Value());

		// 최대 2회: 재사용 연결이 idle 타임아웃으로 서버에 의해 닫혔으면 멱등 메서드에 한해 1회 재연결한다.
		// 재연결은 항상 HTTP/1.1 로 다시 수립한다(이 세션은 이미 h1 로 확정된 상태 — 프로토콜이 흔들리지 않게).
		for (int_t attempt = 0; attempt < 2; ++attempt)
		{
			const bool_t wasReused = stream != nullptr && stream->IsOpen();

			if (!wasReused)
			{
				auto established = co_await internal::EstablishStream(endpoint.host, endpoint.port, endpoint.isSecure, *context, _stopToken);
				if (established.IsError()) co_return R::Error(std::move(established.Error()));

				stream = std::move(established.Value());
				reader = std::make_unique<http_1::internal::MessageReader>(*stream);
			}

			if (auto sent = co_await stream->Send(ne::memory::BufferView{ const_cast<byte_t*>(reinterpret_cast<const byte_t*>(head.data())), head.size() }, _stopToken); sent.IsError())
			{
				Close();
				if (wasReused && idempotent && attempt == 0) continue;
				co_return R::Error(HttpError(std::move(sent.Error())).Context("[ClientSession/Send]"));
			}

			if (!_request.body.IsEmpty())
			{
				if (auto sent = co_await stream->Sendv(_request.body.View(), _stopToken); sent.IsError())
				{
					Close();
					if (wasReused && idempotent && attempt == 0) continue;
					co_return R::Error(HttpError(std::move(sent.Error())).Context("[ClientSession/Send]"));
				}
			}

			auto response = co_await reader->ReadResponse(_stopToken);
			if (response.IsError())
			{
				Close();
				if (wasReused && idempotent && attempt == 0) continue;
				co_return R::Error(std::move(response.Error()));
			}

			// 서버가 close 를 알렸거나 요청이 close 였으면 접는다. 아니면 재사용을 위해 열어 둔다.
			if (WantsClose(response.Value().headers) || WantsClose(_request.headers)) Close();

			co_return R::Ok(std::move(response.Value()));
		}

		co_return R::Error(HttpError(HttpErrorKind::TRANSPORT, "connection retry exhausted").Context("[ClientSession/Send]"));
	}



	/*--------------------------------------------------*/



	SyncClientSession::SyncClientSession(Endpoint _endpoint, const ne::io::EngineType _engineType, const http::Version _version)
		: runtime(std::make_unique<ne::io::Runtime>(_engineType))
		, session(std::make_unique<ClientSession>(std::move(_endpoint), runtime->GetContext(), _version)) {}

	SyncClientSession::~SyncClientSession()
	{
		// 자체 Runtime 을 파괴하기 전에 h2 드라이버를 정상 종료한다(in-flight op 를 문 채 엔진 소멸 방지).
		if (session && runtime && runtime->IsValid()) runtime->BlockOn(session->Shutdown());
	}

	SyncClientSession::SyncClientSession(SyncClientSession&&) noexcept = default;
	SyncClientSession& SyncClientSession::operator=(SyncClientSession&&) noexcept = default;

	bool_t SyncClientSession::IsOpen() const noexcept { return session->IsOpen(); }
	void_t SyncClientSession::Close() { session->Close(); }

	HttpResult<Response> SyncClientSession::Send(ClientBuilder _request)
	{
		if (!runtime->IsValid()) return HttpResult<Response>::Error(HttpError{ HttpErrorKind::TRANSPORT, "I/O runtime initialization failed" });

		return runtime->BlockOn(session->Send(std::move(_request)));
	}



	/*--------------------------------------------------*/



	std::optional<ClientSession> Connect(const string_view_t _url, ne::io::Context& _context, const http::Version _version)
	{
		const auto parsed = internal::ParseUrl(_url);
		if (!parsed) return std::nullopt;

		return ClientSession(Endpoint{ parsed->host, parsed->port, parsed->isSecure }, _context, _version);
	}

	std::optional<SyncClientSession> ConnectSync(const string_view_t _url, const ne::io::EngineType _engineType, const http::Version _version)
	{
		const auto parsed = internal::ParseUrl(_url);
		if (!parsed) return std::nullopt;

		return SyncClientSession(Endpoint{ parsed->host, parsed->port, parsed->isSecure }, _engineType, _version);
	}
}
