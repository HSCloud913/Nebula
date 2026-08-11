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
#include "Network/Protocol/Http/Internal/Timeout.h"
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
		// _suppressBody 가 true 면 헤드만 보낸다(HEAD 응답 — 헤더는 GET 과 동일해야 하고 본문만 없다).
		ne::Task<http::HttpResult<void_t>> SendResponse(IStream& _stream, const http::Response& _response, const bool_t _suppressBody, std::stop_token _stopToken)
		{
			using R = http::HttpResult<void_t>;

			auto builtHead = BuildResponseHead(_response);
			if (builtHead.IsError()) co_return R::Error(std::move(builtHead.Error()));

			const string_t head = std::move(builtHead.Value());
			if (auto sent = co_await _stream.Send(ne::memory::BufferView{ const_cast<byte_t*>(reinterpret_cast<const byte_t*>(head.data())), head.size() }, _stopToken); sent.IsError()) co_return R::Error(http::HttpError(std::move(sent.Error())).Context("[Server/SendResponse]"));

			if (_suppressBody) co_return R::Ok();

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
			co_return co_await SendResponse(_stream, response, false, std::move(_stopToken));
		}
	}



	ne::Task<http::HttpResult<void_t>> Server::HandleEstablished(std::unique_ptr<IStream> _stream, ne::io::Context& _context, std::stop_token _stopToken) const
	{
		using R = http::HttpResult<void_t>;

		std::unique_ptr<IStream> stream = std::move(_stream);

		// 리더는 연결당 하나 — 요청 사이에 버퍼에 남은 바이트(다음 요청의 시작)를 이어서 소비한다.
		MessageReader reader(*stream, limits);

		std::size_t handled = 0;

		while (!_stopToken.stop_requested())
		{
			// 연결당 요청 수 상한 — 무한 keep-alive 로 연결을 붙잡아 두는 것을 막는다.
			if (limits.maxRequestsPerConnection > 0 && handled >= limits.maxRequestsPerConnection) break;

			// 읽기 데드라인. 첫 요청은 헤더 대기(headerTimeout)부터 시작하고, keep-alive 재사용 시에는
			// 다음 요청이 시작되기까지의 유휴 시간(idleTimeout)이 포함된다. 본문까지 한 번에 읽으므로
			// bodyTimeout 을 더해 하나의 예산으로 쓴다 — 파서 내부 단계별 데드라인은 후속 과제.
			const auto readBudget = (handled == 0 ? limits.headerTimeout : limits.idleTimeout) + limits.bodyTimeout;

			auto request = co_await http::internal::WithTimeout(_context, readBudget, [&reader](std::stop_token _token) { return reader.ReadRequest(std::move(_token)); });
			++handled;

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

			// HEAD 응답에 본문을 실으면 keep-alive 프레이밍이 깨져 이후 모든 응답이 오염된다.
			// Content-Length 는 GET 과 동일하게 유지하고(RFC 9110 §9.3.2) 본문 전송만 생략한다.
			const bool_t suppressBody = request.Value().method == http::Method::HEAD;

			if (auto sent = co_await SendResponse(*stream, res, suppressBody, _stopToken); sent.IsError())
			{
				(void_t)stream->Close();
				co_return R::Error(std::move(sent.Error()));
			}

			if (!keepAlive) break;
		}

		(void_t)stream->Close();
		co_return R::Ok();
	}
}
