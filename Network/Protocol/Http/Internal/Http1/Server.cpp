//
// Created by hscloud on 26. 7. 20.
//

#include "Network/Protocol/Http/Internal/Http1/Server.h"

#include <charconv>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "Base/Coroutine/Event.h"
#include "Network/Protocol/Http/Internal/Http1/Parser.h"
#include "Network/Stream/PlainStream.h"
#include "Util/StringFormat.h"



namespace ne::network::http_1::internal
{
	namespace
	{
		bool_t WantsClose(const http::Headers& _headers) noexcept
		{
			const auto connection = _headers.Get("Connection");
			return connection && ne::util::StringFormat::EqualCaseInsensitive(string_view_t(*connection), string_view_t("close"));
		}

		// Accept 된 소켓을 TLS(설정 시)/평문 IStream 으로 감싼다. HandleOne/HandleConnection 이 공유한다.
		ne::Task<http::HttpResult<std::unique_ptr<IStream>>> AcceptStream(ne::io::Socket&& _socket, ne::io::Context& _context, const TlsConfig* _tlsConfig, std::stop_token _stopToken)
		{
			using R = http::HttpResult<std::unique_ptr<IStream>>;

			if (_tlsConfig != nullptr)
			{
				auto tls = co_await TlsStream::Accept(std::move(_socket), _context, *_tlsConfig, _stopToken);
				if (tls.IsError()) co_return R::Error(http::HttpError(std::move(tls.Error())).Context("[Server/AcceptStream]"));

				co_return R::Ok(std::make_unique<TlsStream>(std::move(tls.Value())));
			}

			auto plain = PlainStream::Create(std::move(_socket), _context);
			if (plain.IsError()) co_return R::Error(http::HttpError(std::move(plain.Error())).Context("[Server/AcceptStream]"));

			co_return R::Ok(std::make_unique<PlainStream>(std::move(plain.Value())));
		}

		// 스트리밍 본문을 chunked 프레이밍으로 보낸다 — 생산자를 반복 호출해 조각마다 "<hex>\r\n<data>\r\n",
		// 빈 조각(EOF)에서 종료 청크 "0\r\n\r\n". 생산자 실패 시 chunked 를 종결할 수 없으므로 에러 반환(연결 폐기).
		ne::Task<http::HttpResult<void_t>> SendChunkedBody(IStream& _stream, const http::BodyProducer& _producer, std::stop_token _stopToken)
		{
			using R = http::HttpResult<void_t>;

			while (true)
			{
				auto chunk = co_await _producer();
				if (chunk.IsError()) co_return R::Error(std::move(chunk.Error()).Context("[Server/SendChunkedBody]"));

				const std::vector<byte_t>& data = chunk.Value();

				std::vector<byte_t> frame;
				frame.reserve(data.size() + 24);

				char sizeLine[18];
				const auto [end, ec] = std::to_chars(sizeLine, sizeLine + sizeof(sizeLine), data.size(), 16);
				frame.insert(frame.end(), reinterpret_cast<const byte_t*>(sizeLine), reinterpret_cast<const byte_t*>(end));
				frame.push_back('\r'); frame.push_back('\n');
				frame.insert(frame.end(), data.begin(), data.end());
				frame.push_back('\r'); frame.push_back('\n'); // 빈 조각이면 이 프레임 자체가 종료 청크 "0\r\n\r\n"

				if (auto sent = co_await _stream.Send(ne::memory::BufferView{ frame.data(), frame.size() }, _stopToken); sent.IsError()) co_return R::Error(http::HttpError(std::move(sent.Error())).Context("[Server/SendChunkedBody]"));

				if (data.empty()) co_return R::Ok();
			}
		}

		// 요청 읽기 실패를 클라이언트가 알 수 있게 상태 코드로 응답한다(431 헤더 초과 / 413 본문 초과 / 400 형식 오류).
		ne::Task<http::HttpResult<void_t>> SendReadErrorResponse(IStream& _stream, const http::HttpError& _error, std::stop_token _stopToken);

		// 하나의 응답을 직렬화해 스트림으로 보낸다(헤드 + 본문 — 스트리밍 본문이면 chunked).
		ne::Task<http::HttpResult<void_t>> SendResponse(IStream& _stream, const http::Response& _response, std::stop_token _stopToken)
		{
			using R = http::HttpResult<void_t>;

			auto builtHead = BuildResponseHead(_response);
			if (builtHead.IsError()) co_return R::Error(std::move(builtHead.Error()));

			const string_t head = std::move(builtHead.Value());
			if (auto sent = co_await _stream.Send(ne::memory::BufferView{ const_cast<byte_t*>(reinterpret_cast<const byte_t*>(head.data())), head.size() }, _stopToken); sent.IsError()) co_return R::Error(http::HttpError(std::move(sent.Error())).Context("[Server/SendResponse]"));

			if (_response.body.IsStreaming()) co_return co_await SendChunkedBody(_stream, *_response.body.Producer(), std::move(_stopToken));

			if (!_response.body.IsEmpty())
			{
				if (auto sent = co_await _stream.Sendv(_response.body.View(), _stopToken); sent.IsError()) co_return R::Error(http::HttpError(std::move(sent.Error())).Context("[Server/SendResponse]"));
			}

			co_return R::Ok();
		}

		ne::Task<http::HttpResult<void_t>> SendReadErrorResponse(IStream& _stream, const http::HttpError& _error, std::stop_token _stopToken)
		{
			int_t status = 0;
			switch (_error.Kind())
			{
				case http::HttpErrorKind::HEADER_TOO_LARGE:    status = 431; break;
				case http::HttpErrorKind::BODY_TOO_LARGE:      status = 413; break;
				case http::HttpErrorKind::MALFORMED_MESSAGE:   status = 400; break;
				case http::HttpErrorKind::UNSUPPORTED_VERSION: status = 505; break;
				default: break; // TRANSPORT/TIMEOUT/CONNECTION_CLOSED — 응답을 보낼 수 없거나 무의미
			}
			if (status == 0) co_return http::HttpResult<void_t>::Ok();

			http::Response response = http::Response::Status(status);
			response.headers.Set("Content-Length", "0");
			response.headers.Set("Connection", "close");
			co_return co_await SendResponse(_stream, response, std::move(_stopToken));
		}
	}



	ne::Task<http::HttpResult<void_t>> Server::HandleOne(ne::io::Socket _socket, ne::io::Context& _context, std::stop_token _stopToken) const
	{
		using R = http::HttpResult<void_t>;

		auto streamResult = co_await AcceptStream(std::move(_socket), _context, tlsConfig, _stopToken);
		if (streamResult.IsError()) co_return R::Error(std::move(streamResult.Error()));

		std::unique_ptr<IStream> stream = std::move(streamResult.Value());

		MessageReader reader(*stream, limits);
		auto request = co_await reader.ReadRequest(_stopToken);
		if (request.IsError())
		{
			(void_t)co_await SendReadErrorResponse(*stream, request.Error(), _stopToken);
			(void_t)stream->Close();
			co_return R::Error(std::move(request.Error()));
		}

		auto response = co_await handler(request.Value());
		if (response.IsError())
		{
			(void_t)stream->Close();
			co_return R::Error(std::move(response.Error()));
		}

		http::Response& res = response.Value();
		if (res.body.IsStreaming()) { if (!res.headers.Has("Transfer-Encoding")) res.headers.Set("Transfer-Encoding", "chunked"); }
		else if (!res.body.IsEmpty() && !res.headers.Has("Content-Length")) res.headers.Set("Content-Length", std::to_string(res.body.Size()));
		if (!res.headers.Has("Connection")) res.headers.Set("Connection", "close");

		auto sent = co_await SendResponse(*stream, res, _stopToken);
		(void_t)stream->Close();
		if (sent.IsError()) co_return R::Error(std::move(sent.Error()));

		co_return R::Ok();
	}

	ne::Task<http::HttpResult<void_t>> Server::HandleConnection(ne::io::Socket _socket, ne::io::Context& _context, std::stop_token _stopToken) const
	{
		using R = http::HttpResult<void_t>;

		auto streamResult = co_await AcceptStream(std::move(_socket), _context, tlsConfig, _stopToken);
		if (streamResult.IsError()) co_return R::Error(std::move(streamResult.Error()));

		co_return co_await HandleEstablished(std::move(streamResult.Value()), std::move(_stopToken));
	}

	ne::Task<http::HttpResult<void_t>> Server::HandleEstablished(std::unique_ptr<IStream> _stream, std::stop_token _stopToken) const
	{
		using R = http::HttpResult<void_t>;

		std::unique_ptr<IStream> stream = std::move(_stream);

		// 리더는 연결당 하나 — 요청 사이에 버퍼에 남은 바이트(다음 요청의 시작)를 이어서 소비한다.
		MessageReader reader(*stream, limits);

		while (!_stopToken.stop_requested())
		{
			auto request = co_await reader.ReadRequest(_stopToken);
			if (request.IsError())
			{
				// keep-alive 연결에서 피어가 조용히 닫으면(CONNECTION_CLOSED) 정상 종료로 본다.
				if (request.Error().Kind() == http::HttpErrorKind::CONNECTION_CLOSED)
				{
					(void_t)stream->Close();
					co_return R::Ok();
				}

				// 크기 초과/형식 오류는 조용히 끊지 않고 상태 코드로 알린 뒤 닫는다(클라이언트가 원인을 알 수 있게).
				(void_t)co_await SendReadErrorResponse(*stream, request.Error(), _stopToken);
				(void_t)stream->Close();
				co_return R::Error(std::move(request.Error()));
			}

			const bool_t clientWantsClose = WantsClose(request.Value().headers);

			auto response = co_await handler(request.Value());
			if (response.IsError())
			{
				(void_t)stream->Close();
				co_return R::Error(std::move(response.Error()));
			}

			http::Response& res = response.Value();

			// keep-alive 프레이밍: 스트리밍 본문은 chunked 로(경계가 종료 청크), 바이트 본문은 Content-Length 로(빈 본문은 0).
			if (res.body.IsStreaming()) { if (!res.headers.Has("Transfer-Encoding")) res.headers.Set("Transfer-Encoding", "chunked"); }
			else if (!res.headers.Has("Content-Length") && !res.headers.Has("Transfer-Encoding")) res.headers.Set("Content-Length", std::to_string(res.body.Size()));

			const bool_t keepAlive = !clientWantsClose && !_stopToken.stop_requested();
			res.headers.Set("Connection", keepAlive ? "keep-alive" : "close");

			if (auto sent = co_await SendResponse(*stream, res, _stopToken); sent.IsError())
			{
				(void_t)stream->Close();
				co_return R::Error(std::move(sent.Error()));
			}

			if (!keepAlive) break;
		}

		(void_t)stream->Close();
		co_return R::Ok();
	}

	ne::Task<void_t> Server::RunConnection(ne::io::Socket _socket, ne::io::Context& _context, std::stop_token _stopToken, std::size_t& _active, ne::Event& _allDone) const
	{
		// 연결 하나의 처리 실패가 전체 Accept 루프를 끊지 않도록, HandleConnection() 의 에러는 무시한다.
		(void_t)co_await HandleConnection(std::move(_socket), _context, std::move(_stopToken));

		// SignalDeferred: Serve 는 깨어나면 이 태스크의 프레임을 파괴하므로, 이 프레임이 완전히
		// 끝난 뒤(다음 tick) 재개되도록 지연 신호한다 — 실행 중인 프레임 파괴 방지.
		if (--_active == 0) _allDone.SignalDeferred(_context);
	}

	ne::Task<http::HttpResult<void_t>> Server::Serve(ne::io::Socket _listener, ne::io::Context& _context, std::stop_token _stopToken) const
	{
		using R = http::HttpResult<void_t>;

		// 각 연결은 독립 태스크로 동시 처리한다 — 느린/유휴 keep-alive 연결이 다른 연결의 accept 를 막지 않는다.
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
				if (!_stopToken.stop_requested()) result = R::Error(http::HttpError(std::move(accepted.Error())).Context("[Server/Serve]"));
				break;
			}

			std::erase_if(connections, [](const ne::Task<void_t>& _task) { return _task.IsReady(); });

			++active;
			connections.push_back(RunConnection(std::move(accepted.Value()), _context, connectionStop.get_token(), active, allDone));
			connections.back().Resume();
		}

		// 남은 연결의 I/O 를 취소하고 전부 끝날 때까지 기다린 뒤 반환한다 — in-flight op 를 문 채
		// 반환하면 호출자가 엔진을 파괴할 때 완료가 파괴된 엔진으로 배달되어 크래시할 수 있다.
		connectionStop.request_stop();
		while (active > 0) co_await allDone;

		co_return result;
	}
}
