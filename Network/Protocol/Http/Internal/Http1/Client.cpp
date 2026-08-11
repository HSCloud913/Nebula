//
// Created by hscloud on 26. 7. 20.
//

#include "Network/Protocol/Http/Internal/Http1/Client.h"

#include <memory>
#include <string>
#include <utility>
#include "Network/Protocol/Http/Internal/Http1/Parser.h"
#include "Network/Protocol/Http/Internal/Transport.h"



namespace ne::network::http_1::internal
{
	namespace
	{
		// 상태 없는 Client 의 요청 기본 헤더를 채운다(Host / Connection: close / Content-Length).
		void_t ApplyClientDefaults(http::Request& _request, const http::Endpoint& _endpoint)
		{
			if (!_request.headers.Has("Host")) _request.headers.Set("Host", _endpoint.host);
			if (!_request.headers.Has("Connection")) _request.headers.Set("Connection", "close");
			if (!_request.body.IsEmpty() && !_request.headers.Has("Content-Length")) _request.headers.Set("Content-Length", std::to_string(_request.body.Size()));
		}

		// 요청 헤드(CR/LF 인젝션 검증 포함) + 본문을 스트림으로 전송한다. Request/Stream 공유 송신 경로.
		ne::Task<http::HttpResult<void_t>> SendRequestOver(IStream& _stream, const http::Request& _request, std::stop_token _stopToken)
		{
			using R = http::HttpResult<void_t>;

			// 스트리밍 본문(BodyProducer)은 서버 응답 전용 — 조용히 Content-Length: 0 으로 나가는 것을 막는다.
			if (_request.body.IsStreaming()) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "streaming request body is not supported").Context("[Client/Send]"));

			auto built = BuildRequestHead(_request);
			if (built.IsError()) co_return R::Error(std::move(built.Error()));

			const string_t head = std::move(built.Value());
			if (auto sent = co_await _stream.Send(ne::memory::BufferView{ const_cast<byte_t*>(reinterpret_cast<const byte_t*>(head.data())), head.size() }, _stopToken); sent.IsError()) co_return R::Error(http::HttpError(std::move(sent.Error())).Context("[Client/Send]"));

			if (!_request.body.IsEmpty())
			{
				if (auto sent = co_await _stream.Sendv(_request.body.View(), _stopToken); sent.IsError()) co_return R::Error(http::HttpError(std::move(sent.Error())).Context("[Client/Send]"));
			}

			co_return R::Ok();
		}
	}



	ne::Task<http::HttpResult<http::Response>> Client::Request(const http::Endpoint& _endpoint, http::Request _request, ne::io::Context& _context, std::stop_token _stopToken)
	{
		using R = http::HttpResult<http::Response>;

		auto established = co_await http::internal::EstablishStream(_endpoint.host, _endpoint.port, _endpoint.isSecure, _context, _stopToken);
		if (established.IsError()) co_return R::Error(std::move(established.Error()));

		co_return co_await RequestOver(std::move(established.Value()), _endpoint, std::move(_request), std::move(_stopToken));
	}

	ne::Task<http::HttpResult<http::Response>> Client::RequestOver(std::unique_ptr<IStream> _stream, const http::Endpoint& _endpoint, http::Request _request, std::stop_token _stopToken)
	{
		using R = http::HttpResult<http::Response>;

		ApplyClientDefaults(_request, _endpoint);

		std::unique_ptr<IStream> stream = std::move(_stream);

		if (auto sent = co_await SendRequestOver(*stream, _request, _stopToken); sent.IsError()) co_return R::Error(std::move(sent.Error()));

		MessageReader reader(*stream);
		auto response = co_await reader.ReadResponse(_stopToken);

		(void_t)stream->Close();

		if (response.IsError()) co_return R::Error(std::move(response.Error()));

		co_return R::Ok(std::move(response.Value()));
	}

	ne::Task<http::HttpResult<http::Response>> Client::Request(const string_view_t _url, http::Request _request, ne::io::Context& _context, std::stop_token _stopToken)
	{
		using R = http::HttpResult<http::Response>;

		const auto parsed = http::internal::ParseUrl(_url);
		if (!parsed) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "invalid URL").Context("[Client/Request]"));

		_request.target = parsed->target;

		co_return co_await Request(http::Endpoint{ parsed->host, parsed->port, parsed->isSecure }, std::move(_request), _context, std::move(_stopToken));
	}



	ne::Task<http::HttpResult<void_t>> Client::Stream(const http::Endpoint& _endpoint, http::Request _request, http::ResponseCallbacks _sink, ne::io::Context& _context, std::stop_token _stopToken)
	{
		using R = http::HttpResult<void_t>;

		auto established = co_await http::internal::EstablishStream(_endpoint.host, _endpoint.port, _endpoint.isSecure, _context, _stopToken);
		if (established.IsError()) co_return R::Error(std::move(established.Error()));

		co_return co_await StreamOver(std::move(established.Value()), _endpoint, std::move(_request), std::move(_sink), std::move(_stopToken));
	}

	ne::Task<http::HttpResult<void_t>> Client::StreamOver(std::unique_ptr<IStream> _stream, const http::Endpoint& _endpoint, http::Request _request, http::ResponseCallbacks _sink, std::stop_token _stopToken)
	{
		using R = http::HttpResult<void_t>;

		ApplyClientDefaults(_request, _endpoint);

		std::unique_ptr<IStream> stream = std::move(_stream);

		if (auto sent = co_await SendRequestOver(*stream, _request, _stopToken); sent.IsError()) co_return R::Error(std::move(sent.Error()));

		MessageReader reader(*stream);
		auto streamed = co_await reader.ReadResponseStreaming(_sink, _stopToken); // _sink 는 이 프레임이 소유(수명 안전)

		(void_t)stream->Close();

		if (streamed.IsError()) co_return R::Error(std::move(streamed.Error()));

		co_return R::Ok();
	}

	ne::Task<http::HttpResult<void_t>> Client::Stream(const string_view_t _url, http::Request _request, http::ResponseCallbacks _sink, ne::io::Context& _context, std::stop_token _stopToken)
	{
		using R = http::HttpResult<void_t>;

		const auto parsed = http::internal::ParseUrl(_url);
		if (!parsed) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "invalid URL").Context("[Client/Stream]"));

		_request.target = parsed->target;

		co_return co_await Stream(http::Endpoint{ parsed->host, parsed->port, parsed->isSecure }, std::move(_request), std::move(_sink), _context, std::move(_stopToken));
	}
}
