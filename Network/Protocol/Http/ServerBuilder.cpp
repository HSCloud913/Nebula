//
// Created by hscloud on 26. 7. 30.
//

#include "Network/Protocol/Http/ServerBuilder.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include "Base/Coroutine/Event.h"
#include "Network/Stream/PlainStream.h"
#include "Network/Protocol/Http/Internal/Router.h"
#include "Network/Protocol/Http/Internal/Http1/Server.h"
#include "Network/Protocol/Http/Internal/Http2/Connection.h"



namespace ne::network::http
{
	namespace
	{
		// 수립된 스트림 + ALPN 협상 결과(평문이면 빈 문자열).
		struct AcceptedStream
		{
			std::unique_ptr<ne::network::IStream> stream;
			string_t negotiatedProtocol;
		};

		// Accept 된 소켓을 TLS(설정 시)/평문 IStream 으로 감싼다. TLS 는 ALPN 협상 결과를 함께 돌려준다.
		ne::Task<HttpResult<AcceptedStream>> AcceptStream(ne::io::Socket&& _socket, ne::io::Context& _context, const TlsConfig* _tlsConfig, std::stop_token _stopToken)
		{
			using R = HttpResult<AcceptedStream>;

			if (_tlsConfig != nullptr)
			{
				auto tls = co_await ne::network::TlsStream::Accept(std::move(_socket), _context, *_tlsConfig, _stopToken);
				if (tls.IsError()) co_return R::Error(HttpError(std::move(tls.Error())).Context("[HttpServer/AcceptStream]"));

				string_t negotiated{ tls.Value().NegotiatedProtocol() };
				co_return R::Ok(AcceptedStream{ std::make_unique<ne::network::TlsStream>(std::move(tls.Value())), std::move(negotiated) });
			}

			auto plain = ne::network::PlainStream::Create(std::move(_socket), _context);
			if (plain.IsError()) co_return R::Error(HttpError(std::move(plain.Error())).Context("[HttpServer/AcceptStream]"));

			co_return R::Ok(AcceptedStream{ std::make_unique<ne::network::PlainStream>(std::move(plain.Value())), string_t{} });
		}

		// 이 연결을 HTTP/2 로 처리할지 결정한다. AUTO 는 ALPN 결과("h2")로만 h2 를 고른다 — 평문은 협상이
		// 없으므로 항상 HTTP/1.1 (h2c prior knowledge 는 Version(HTTP_2) 명시 전제).
		bool_t UseHttp2(const Version _version, const string_view_t _negotiatedProtocol) noexcept
		{
			if (_version == Version::HTTP_2) return true;
			if (_version == Version::HTTP_1_1) return false;

			return _negotiatedProtocol == "h2";
		}

		// 연결 하나를 배경 태스크로 처리하고, 마지막 활성 연결이 끝나면 _allDone 을 신호한다(Serve 의 drain 용).
		// TLS 핸드셰이크(AcceptStream)도 이 태스크 안에서 수행 — 느린 핸드셰이크가 accept 루프를 막지 않는다.
		ne::Task<void_t> RunConnection(ne::io::Socket _socket, ne::io::Context& _context, const TlsConfig* _tlsConfig, const Version _version, const http_1::internal::Server& _http1Engine, const ServerBuilder::Handler& _handler, const Limits _limits, const ServerObserver* _observer, std::stop_token _stopToken, std::size_t& _active, ne::Event& _allDone)
		{
			if (_observer != nullptr && _observer->onConnection) _observer->onConnection(true);

			// 연결 하나의 실패는 이 연결만의 문제 — 흐름은 끊지 않고 관측 훅으로만 알린다.
			auto accepted = co_await AcceptStream(std::move(_socket), _context, _tlsConfig, _stopToken);
			if (accepted.IsError())
			{
				if (_observer != nullptr && _observer->onError) _observer->onError(accepted.Error(), _tlsConfig != nullptr ? "Tls" : "Accept");
			}
			else
			{
				if (UseHttp2(_version, accepted.Value().negotiatedProtocol))
				{
					// Connection 은 heap 고정 계약(NON_COPYABLE_MOVABLE)을 따른다.
					const auto connection = std::make_unique<http_2::internal::ServerConnection>(std::move(accepted.Value().stream), _context, _handler, _limits, _observer);
					if (auto ran = co_await connection->Run(std::move(_stopToken)); ran.IsError() && _observer != nullptr && _observer->onError) _observer->onError(ran.Error(), "Frame");
				}
				else
				{
					(void_t)co_await _http1Engine.HandleEstablished(std::move(accepted.Value().stream), _context, std::move(_stopToken));
				}
			}

			if (_observer != nullptr && _observer->onConnection) _observer->onConnection(false);

			// SignalDeferred: Serve 는 깨어나면 이 태스크의 프레임을 파괴하므로, 이 프레임이 완전히
			// 끝난 뒤(다음 tick) 재개되도록 지연 신호한다 — 실행 중인 프레임 파괴 방지.
			if (--_active == 0) _allDone.SignalDeferred(_context);
		}
	}



	ServerBuilder::Handler ServerBuilder::BuildHandler() const
	{
		// 라우트 스냅샷을 Router 로 조립해 클로저가 소유한다.
		internal::Router router;
		for (const auto& entry : routes) router.Add(entry.method, entry.path, entry.handler);
		if (notFound) router.SetNotFound(notFound);

		return [router = std::move(router)](const Request& _request) { return router.Dispatch(_request); };
	}

	ne::Task<HttpResult<void_t>> ServerBuilder::Serve(ne::io::Socket _listener, ne::io::Context& _context, std::stop_token _stopToken) const
	{
		using R = HttpResult<void_t>;

		const Handler handler = BuildHandler();

		// HTTP/1.1 엔진은 무상태(핸들러/한도만 소유)라 모든 연결이 공유한다. TLS accept 는 이 계층
		// (AcceptStream)에서 이미 끝났으므로 엔진에는 tlsConfig 를 넘기지 않는다.
		const http_1::internal::Server http1Engine(handler, limits, &observer);

		// 각 연결은 독립 태스크로 동시 처리한다 — 느린/유휴 연결이 다른 연결의 accept 를 막지 않는다.
		// 종료 시(외부 stop 또는 Accept 실패) connectionStop 으로 진행 중인 연결들의 I/O 를 일괄 취소한다.
		std::stop_source connectionStop;
		std::stop_callback forwardStop{ _stopToken, [&connectionStop] { connectionStop.request_stop(); } };

		std::size_t active = 0;
		ne::Event allDone;
		std::vector<ne::Task<void_t>> connections; // 연결 태스크 소유 컨테이너(완료 프레임은 다음 accept 때 회수)

		R result = R::Ok();

		while (!_stopToken.stop_requested())
		{
			auto accepted = co_await _listener.Accept(false, _stopToken);
			if (accepted.IsError())
			{
				// stop 요청에 의한 Accept 취소는 정상 종료로 본다.
				if (!_stopToken.stop_requested()) result = R::Error(HttpError(std::move(accepted.Error())).Context("[HttpServer/Serve]"));
				break;
			}

			std::erase_if(connections, [](const ne::Task<void_t>& _task) { return _task.IsReady(); });

			// 동시 연결 상한 — 없으면 accept 루프가 무한정 코루틴 프레임과 버퍼를 쌓는다. 초과분은
			// 즉시 닫아 클라이언트가 바로 알 수 있게 한다(대기열에 넣어 늦게 실패시키는 것보다 낫다).
			if (limits.maxConnections > 0 && active >= limits.maxConnections)
			{
				(void_t)accepted.Value().Close();
				continue;
			}

			++active;
			connections.push_back(RunConnection(std::move(accepted.Value()), _context, tlsConfig, version, http1Engine, handler, limits, &observer, connectionStop.get_token(), active, allDone));
			connections.back().Resume();
		}

		// 남은 연결의 I/O 를 취소하고 전부 끝날 때까지 기다린 뒤 반환한다 — in-flight op 를 문 채
		// 반환하면 호출자가 엔진을 파괴할 때 완료가 파괴된 엔진으로 배달되어 크래시할 수 있다.
		connectionStop.request_stop();
		while (active > 0) co_await allDone;

		co_return result;
	}
}
