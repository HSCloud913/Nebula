//
// Created by hscloud on 26. 7. 20.
//

#include "Network/Protocol/Http/Internal/Http1/Server.h"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "Base/Coroutine/Event.h"
#include "Network/Protocol/Http/Internal/Http1/Parser.h"
#include "Network/Protocol/Http/Internal/Timeout.h"
#include "Network/Stream/PlainStream.h"
#include "Network/Protocol/Http/Message/Date.h"
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
		// _isBodySuppressed 가 true 면 헤드만 보낸다(HEAD 응답 — 헤더는 GET 과 동일해야 하고 본문만 없다).
		ne::Task<http::HttpResult<void_t>> SendResponse(IStream& _stream, const http::Response& _response, const bool_t _isBodySuppressed, std::stop_token _stopToken)
		{
			using R = http::HttpResult<void_t>;

			auto builtHead = BuildResponseHead(_response);
			if (builtHead.IsError()) co_return R::Error(std::move(builtHead.Error()));

			const string_t head = std::move(builtHead.Value());
			if (auto sent = co_await _stream.Send(ne::memory::BufferView{ const_cast<byte_t*>(reinterpret_cast<const byte_t*>(head.data())), head.size() }, _stopToken); sent.IsError()) co_return R::Error(http::HttpError(std::move(sent.Error())).Context("[Server/SendResponse]"));

			if (_isBodySuppressed) co_return R::Ok();

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
			// 읽기 데드라인을 **절대 시각으로 한 번** 정한다. 첫 요청은 헤더 대기(headerTimeout)부터,
			// keep-alive 재사용 시에는 다음 요청이 시작되기까지의 유휴 시간(idleTimeout)부터 센다.
			// 본문까지 한 호출에 읽으므로 bodyTimeout 을 더해 하나의 예산으로 만든다.
			//
			// 여기서 Deadline 을 쓰는 이유: 상대 지연으로 넘기면 이후 파서 내부 단계에 시한을 더 걸 때
			// 각 단계가 자기 몫을 새로 받아 전체 예산이 단계 수만큼 불어난다. 절대 시각이면 그 전부가
			// 같은 예산을 나눠 쓴다.
			const auto readDeadline = _context.DeadlineAfter((handled == 0 ? limits.headerTimeout : limits.idleTimeout) + limits.bodyTimeout);

			// 타임아웃 시 리더 상태는 메시지 중간에 멈춰 있다. 아래 모든 에러 경로가 연결을 닫으므로
			// 그 상태가 재사용되지 않는다 — 타임아웃을 비치명적으로 바꾸려면 리더도 함께 버려야 한다.
			auto request = co_await http::internal::WithTimeout(_context, readDeadline, [&reader](std::stop_token _token) { return reader.ReadRequest(std::move(_token)); });

			if (request.IsError())
			{
				// keep-alive 연결에서 피어가 조용히 닫으면(CONNECTION_CLOSED) 정상 종료로 본다.
				if (request.Error().Kind() == http::HttpErrorKind::CONNECTION_CLOSED)
				{
					(void_t)stream->Close();
					co_return R::Ok();
				}

				if (observer != nullptr && observer->onError) observer->onError(request.Error(), "Read");

				// 크기 초과/형식 오류는 조용히 끊지 않고 상태 코드로 알린 뒤 닫는다(클라이언트가 원인을 알 수 있게).
				(void_t)co_await SendReadErrorResponse(*stream, request.Error(), _stopToken);
				(void_t)stream->Close();
				co_return R::Error(std::move(request.Error()));
			}

			const bool_t isClientCloseRequested = WantsClose(request.Value().headers);
			const auto handleStart = std::chrono::steady_clock::now();

			auto response = co_await handler(request.Value());
			if (response.IsError())
			{
				if (observer != nullptr && observer->onError) observer->onError(response.Error(), "Dispatch");
				(void_t)stream->Close();
				co_return R::Error(std::move(response.Error()));
			}

			http::Response& res = response.Value();

			// keep-alive 프레이밍: 스트리밍 본문은 chunked 로(경계가 종료 청크), 바이트 본문은 Content-Length 로(빈 본문은 0).
			if (res.body.IsStreaming()) { if (!res.headers.Has("Transfer-Encoding")) res.headers.Set("Transfer-Encoding", "chunked"); }
			else if (!res.headers.Has("Content-Length") && !res.headers.Has("Transfer-Encoding")) res.headers.Set("Content-Length", std::to_string(res.body.Size()));

			++handled;

			// 연결당 요청 수 상한에 도달했다면 **이 응답에** close 를 실어야 한다. 루프 상단에서 조용히
			// break 하면 방금 keep-alive 를 약속한 클라이언트가 다음 요청을 보내다 FIN 과 경합해 잃는다
			// (비멱등 요청은 안전하게 재시도할 수도 없다).
			const bool_t hasReachedRequestLimit = limits.maxRequestsPerConnection > 0 && handled >= limits.maxRequestsPerConnection;

			const bool_t isKeepAlive = !isClientCloseRequested && !hasReachedRequestLimit && !_stopToken.stop_requested();
			res.headers.Set("Connection", isKeepAlive ? "keep-alive" : "close");

			// RFC 9110 §6.6.1: 시계를 가진 오리진 서버는 Date 를 보내야 한다(캐시/조건부 요청의 기준).
			if (!res.headers.Has("Date")) res.headers.Set("Date", http::FormatDate(std::chrono::system_clock::now()));

			// HEAD 응답에 본문을 실으면 keep-alive 프레이밍이 깨져 이후 모든 응답이 오염된다.
			// Content-Length 는 GET 과 동일하게 유지하고(RFC 9110 §9.3.2) 본문 전송만 생략한다.
			const bool_t isBodySuppressed = request.Value().method == http::Method::HEAD;

			const std::size_t responseBytes = res.body.IsStreaming() ? 0 : res.body.Size();

			if (auto sent = co_await SendResponse(*stream, res, isBodySuppressed, _stopToken); sent.IsError())
			{
				if (observer != nullptr && observer->onError) observer->onError(sent.Error(), "Write");

				(void_t)stream->Close();
				co_return R::Error(std::move(sent.Error()));
			}

			if (observer != nullptr && observer->onAccess)
			{
				http::AccessRecord record;
				record.method = request.Value().method;
				record.target = request.Value().target;
				record.statusCode = res.statusCode;
				record.version = http::Version::HTTP_1_1;
				record.requestBodyBytes = request.Value().body.Size();
				record.responseBodyBytes = isBodySuppressed ? 0 : responseBytes;
				record.duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - handleStart);

				observer->onAccess(record);
			}

			if (!isKeepAlive) break;
		}

		(void_t)stream->Close();
		co_return R::Ok();
	}
}
