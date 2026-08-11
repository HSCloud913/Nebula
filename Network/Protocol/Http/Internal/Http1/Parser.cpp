//
// Created by hscloud on 26. 7. 20.
//

#include "Network/Protocol/Http/Internal/Http1/Parser.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <span>
#include "Util/StringFormat.h"

namespace ne::network::http_1::internal
{
	ne::Task<http::HttpResult<void_t>> MessageReader::Fill(std::stop_token _stopToken)
	{
		using R = http::HttpResult<void_t>;

		// 이미 소비된 앞부분을 앞으로 당겨 버퍼를 재사용한다(장수명 body 읽기에서 무한정 커지는 것을 방지).
		if (dataStart > 0)
		{
			std::move(buffer.begin() + static_cast<std::ptrdiff_t>(dataStart), buffer.begin() + static_cast<std::ptrdiff_t>(dataEnd), buffer.begin());
			dataEnd -= dataStart;
			dataStart = 0;
		}

		if (buffer.size() < dataEnd + ReadChunkSize) buffer.resize(dataEnd + ReadChunkSize);

		auto received = co_await stream->Receive(ne::memory::BufferView{ buffer.data() + dataEnd, ReadChunkSize }, _stopToken);
		if (received.IsError()) co_return R::Error(http::HttpError(std::move(received.Error())).Context("[MessageReader/Fill]"));
		if (received.Value() == 0) co_return R::Error(http::HttpError(http::HttpErrorKind::CONNECTION_CLOSED).Context("[MessageReader/Fill]"));

		dataEnd += received.Value();

		co_return R::Ok();
	}

	std::optional<std::size_t> MessageReader::FindCrlf() const noexcept
	{
		if (dataEnd < dataStart + 2) return std::nullopt;

		for (std::size_t i = dataStart; i + 1 < dataEnd; ++i) if (buffer[i] == '\r' && buffer[i + 1] == '\n') return i;

		return std::nullopt;
	}

	ne::Task<http::HttpResult<string_t>> MessageReader::ReadLine(std::stop_token _stopToken)
	{
		using R = http::HttpResult<string_t>;

		while (true)
		{
			if (const auto pos = FindCrlf())
			{
				string_t line(reinterpret_cast<const char_t*>(buffer.data() + dataStart), *pos - dataStart);
				dataStart = *pos + 2;
				co_return R::Ok(std::move(line));
			}

			if (dataEnd - dataStart > limits.maxHeaderBytes) co_return R::Error(http::HttpError(http::HttpErrorKind::HEADER_TOO_LARGE).Context("[MessageReader/ReadLine]"));

			if (auto filled = co_await Fill(_stopToken); filled.IsError()) co_return R::Error(std::move(filled.Error()));
		}
	}

	ne::Task<http::HttpResult<http::Headers>> MessageReader::ReadHeaders(std::stop_token _stopToken)
	{
		using R = http::HttpResult<http::Headers>;

		http::Headers headers;
		std::size_t totalBytes = 0;

		while (true)
		{
			auto line = co_await ReadLine(_stopToken);
			if (line.IsError()) co_return R::Error(std::move(line.Error()));
			if (line.Value().empty()) co_return R::Ok(std::move(headers));

			totalBytes += line.Value().size();
			if (totalBytes > limits.maxHeaderBytes) co_return R::Error(http::HttpError(http::HttpErrorKind::HEADER_TOO_LARGE).Context("[MessageReader/ReadHeaders]"));

			const auto sep = line.Value().find(':');
			if (sep == string_t::npos) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "header line missing ':'").Context("[MessageReader/ReadHeaders]"));

			const string_t name = line.Value().substr(0, sep);
			const string_t value = ne::util::StringFormat::Trim(line.Value().substr(sep + 1));
			headers.Add(name, value);
		}
	}

	ne::Task<http::HttpResult<std::vector<byte_t>>> MessageReader::ReadExact(const std::size_t _length, std::stop_token _stopToken)
	{
		using R = http::HttpResult<std::vector<byte_t>>;

		std::vector<byte_t> result;
		result.reserve(_length);

		while (result.size() < _length)
		{
			if (dataStart == dataEnd)
			{
				if (auto filled = co_await Fill(_stopToken); filled.IsError()) co_return R::Error(std::move(filled.Error()));
				continue;
			}

			const std::size_t want = std::min(_length - result.size(), dataEnd - dataStart);
			result.insert(result.end(), buffer.begin() + static_cast<std::ptrdiff_t>(dataStart), buffer.begin() + static_cast<std::ptrdiff_t>(dataStart + want));
			dataStart += want;
		}

		co_return R::Ok(std::move(result));
	}

	ne::Task<http::HttpResult<http::Body>> MessageReader::ReadBody(const http::Headers& _headers, const bool_t _allowUntilClose, std::stop_token _stopToken)
	{
		using R = http::HttpResult<http::Body>;

		if (const auto transferEncoding = _headers.Get("Transfer-Encoding"); transferEncoding && ne::util::StringFormat::EqualCaseInsensitive(string_view_t(*transferEncoding), string_view_t("chunked"))) co_return co_await ReadChunkedBody(_stopToken);

		if (const auto contentLength = _headers.Get("Content-Length"))
		{
			// 중복 Content-Length 는 프론트엔드/백엔드가 서로 다른 값을 채택하는 전형적인 요청 스머글링
			// 벡터다. Headers::Get 은 첫 값을 돌려주므로 검사 없이는 더 작은 값을 조용히 쓰게 된다.
			if (const auto all = _headers.GetAll("Content-Length"); all.size() > 1)
			{
				for (const auto& value : all) if (value != *contentLength) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "conflicting Content-Length").Context("[MessageReader/ReadBody]"));
			}

			std::size_t length = 0;
			const char* first = contentLength->data();
			const char* last = first + contentLength->size();
			const auto [ptr, ec] = std::from_chars(first, last, length);

			// from_chars 는 접두 숫자만 읽고 멈춘다 — ptr != last 를 검사하지 않으면 "5abc" 가 5 로
			// 통과한다(RFC 9112 §6.1 은 DIGIT 이외를 거부하도록 요구한다).
			if (ec != std::errc{} || ptr != last) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "invalid Content-Length").Context("[MessageReader/ReadBody]"));
			if (length > limits.maxBodyBytes) co_return R::Error(http::HttpError(http::HttpErrorKind::BODY_TOO_LARGE).Context("[MessageReader/ReadBody]"));

			auto bytes = co_await ReadExact(length, _stopToken);
			if (bytes.IsError()) co_return R::Error(std::move(bytes.Error()));

			co_return R::Ok(http::Body{ std::move(bytes.Value()) });
		}

		// 응답에 길이 프레이밍이 없으면 "연결이 닫힐 때까지" 가 본문이다. 예전에는 빈 본문을 돌려줘
		// HTTP/1.0 류 응답의 데이터를 조용히 버렸다.
		if (_allowUntilClose) co_return co_await ReadBodyUntilClose(_stopToken);

		co_return R::Ok(http::Body{});
	}

	ne::Task<http::HttpResult<http::Body>> MessageReader::ReadBodyUntilClose(std::stop_token _stopToken)
	{
		using R = http::HttpResult<http::Body>;

		std::vector<byte_t> result;

		while (true)
		{
			// 버퍼에 이미 들어와 있는 만큼을 먼저 흡수한다.
			if (dataStart < dataEnd)
			{
				result.insert(result.end(), buffer.begin() + static_cast<std::ptrdiff_t>(dataStart), buffer.begin() + static_cast<std::ptrdiff_t>(dataEnd));
				dataStart = dataEnd;

				if (result.size() > limits.maxBodyBytes) co_return R::Error(http::HttpError(http::HttpErrorKind::BODY_TOO_LARGE).Context("[MessageReader/ReadBodyUntilClose]"));
			}

			// Fill 은 상대가 닫으면 CONNECTION_CLOSED 를 돌려준다 — 이 경로에서는 그것이 정상 종료다.
			auto filled = co_await Fill(_stopToken);
			if (filled.IsError())
			{
				if (filled.Error().Kind() == http::HttpErrorKind::CONNECTION_CLOSED) co_return R::Ok(http::Body{ std::move(result) });

				co_return R::Error(std::move(filled.Error()));
			}
		}
	}

	ne::Task<http::HttpResult<http::Body>> MessageReader::ReadChunkedBody(std::stop_token _stopToken)
	{
		using R = http::HttpResult<http::Body>;

		std::vector<byte_t> result;

		while (true)
		{
			auto sizeLine = co_await ReadLine(_stopToken);
			if (sizeLine.IsError()) co_return R::Error(std::move(sizeLine.Error()));

			string_t sizeText = sizeLine.Value();
			if (const auto ext = sizeText.find(';'); ext != string_t::npos) sizeText = sizeText.substr(0, ext);

			std::size_t chunkSize = 0;
			const auto [ptr, ec] = std::from_chars(sizeText.data(), sizeText.data() + sizeText.size(), chunkSize, 16);
			if (ec != std::errc{}) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "invalid chunk size").Context("[MessageReader/ReadChunkedBody]"));

			if (chunkSize == 0)
			{
				// 트레일러 헤더(있다면)를 읽고 버린다 — 빈 줄까지 소비.
				while (true)
				{
					auto trailer = co_await ReadLine(_stopToken);
					if (trailer.IsError()) co_return R::Error(std::move(trailer.Error()));
					if (trailer.Value().empty()) break;
				}

				co_return R::Ok(http::Body{ std::move(result) });
			}

			if (result.size() + chunkSize > limits.maxBodyBytes) co_return R::Error(http::HttpError(http::HttpErrorKind::BODY_TOO_LARGE).Context("[MessageReader/ReadChunkedBody]"));

			auto chunkData = co_await ReadExact(chunkSize, _stopToken);
			if (chunkData.IsError()) co_return R::Error(std::move(chunkData.Error()));

			result.insert(result.end(), chunkData.Value().begin(), chunkData.Value().end());

			// 청크 데이터 뒤의 CRLF 를 소비한다.
			auto crlf = co_await ReadLine(_stopToken);
			if (crlf.IsError()) co_return R::Error(std::move(crlf.Error()));
			if (!crlf.Value().empty()) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "chunk data not followed by CRLF").Context("[MessageReader/ReadChunkedBody]"));
		}
	}

	ne::Task<http::HttpResult<http::Request>> MessageReader::ReadRequest(std::stop_token _stopToken)
	{
		using R = http::HttpResult<http::Request>;

		auto line = co_await ReadLine(_stopToken);
		if (line.IsError()) co_return R::Error(std::move(line.Error()));

		const auto& text = line.Value();
		const auto sp1 = text.find(' ');
		const auto sp2 = sp1 == string_t::npos ? string_t::npos : text.find(' ', sp1 + 1);
		if (sp1 == string_t::npos || sp2 == string_t::npos) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "malformed request line").Context("[MessageReader/ReadRequest]"));

		http::Request request;
		request.method = http::MethodFromString(string_view_t(text).substr(0, sp1));
		request.target = text.substr(sp1 + 1, sp2 - sp1 - 1);

		const string_view_t version(text.data() + sp2 + 1, text.size() - sp2 - 1);
		if (!version.starts_with("HTTP/1.")) co_return R::Error(http::HttpError(http::HttpErrorKind::UNSUPPORTED_VERSION, string_t(version)).Context("[MessageReader/ReadRequest]"));

		auto headers = co_await ReadHeaders(_stopToken);
		if (headers.IsError()) co_return R::Error(std::move(headers.Error()));
		request.headers = std::move(headers.Value());

		auto body = co_await ReadBody(request.headers, false, _stopToken);
		if (body.IsError()) co_return R::Error(std::move(body.Error()));
		request.body = std::move(body.Value());

		co_return R::Ok(std::move(request));
	}

	ne::Task<http::HttpResult<http::Response>> MessageReader::ReadResponse(const http::Method _requestMethod, std::stop_token _stopToken)
	{
		using R = http::HttpResult<http::Response>;

		// 1xx 는 최종 응답이 아니다 — 상태줄+헤더만 소비하고 다음 응답을 계속 읽는다(본문은 없다).
		while (true)
		{
			auto response = co_await ReadOneResponse(_requestMethod, _stopToken);
			if (response.IsError()) co_return R::Error(std::move(response.Error()));

			if (response.Value().statusCode >= 100 && response.Value().statusCode < 200) continue;

			co_return R::Ok(std::move(response.Value()));
		}
	}

	ne::Task<http::HttpResult<http::Response>> MessageReader::ReadOneResponse(const http::Method _requestMethod, std::stop_token _stopToken)
	{
		using R = http::HttpResult<http::Response>;

		auto line = co_await ReadLine(_stopToken);
		if (line.IsError()) co_return R::Error(std::move(line.Error()));

		const auto& text = line.Value();
		const auto sp1 = text.find(' ');
		const auto sp2 = sp1 == string_t::npos ? string_t::npos : text.find(' ', sp1 + 1);
		if (sp1 == string_t::npos) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "malformed status line").Context("[MessageReader/ReadResponse]"));

		const string_view_t version(text.data(), sp1);
		if (!version.starts_with("HTTP/1.")) co_return R::Error(http::HttpError(http::HttpErrorKind::UNSUPPORTED_VERSION, string_t(version)).Context("[MessageReader/ReadResponse]"));

		http::Response response;

		const string_t codeText = sp2 == string_t::npos ? text.substr(sp1 + 1) : text.substr(sp1 + 1, sp2 - sp1 - 1);
		const auto [ptr, ec] = std::from_chars(codeText.data(), codeText.data() + codeText.size(), response.statusCode);
		if (ec != std::errc{}) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "invalid status code").Context("[MessageReader/ReadResponse]"));

		if (sp2 != string_t::npos) response.reason = text.substr(sp2 + 1);

		auto headers = co_await ReadHeaders(_stopToken);
		if (headers.IsError()) co_return R::Error(std::move(headers.Error()));
		response.headers = std::move(headers.Value());

		// HEAD/1xx/204/304 는 프레이밍 헤더가 무엇이든 본문이 없다. 이것을 무시하고 Content-Length 를
		// 믿으면 서버가 절대 보내지 않을 바이트를 기다리며 멈춘다.
		if (ResponseHasNoBody(response.statusCode, _requestMethod)) co_return R::Ok(std::move(response));

		auto body = co_await ReadBody(response.headers, true, _stopToken);
		if (body.IsError()) co_return R::Error(std::move(body.Error()));
		response.body = std::move(body.Value());

		co_return R::Ok(std::move(response));
	}

	ne::Task<http::HttpResult<bool_t>> MessageReader::ReadResponseStreaming(const http::ResponseCallbacks& _sink, const http::Method _requestMethod, std::stop_token _stopToken)
	{
		using R = http::HttpResult<bool_t>;

		int_t statusCode = 0;
		string_t reason;
		http::Headers headers;

		// 1xx(정보) 응답은 최종 응답이 아니므로 사용자 콜백에 올리지 않고 건너뛴다.
		while (true)
		{
			auto line = co_await ReadLine(_stopToken);
			if (line.IsError()) co_return R::Error(std::move(line.Error()));

			const auto& text = line.Value();
			const auto sp1 = text.find(' ');
			const auto sp2 = sp1 == string_t::npos ? string_t::npos : text.find(' ', sp1 + 1);
			if (sp1 == string_t::npos) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "malformed status line").Context("[MessageReader/ReadResponseStreaming]"));

			const string_view_t version(text.data(), sp1);
			if (!version.starts_with("HTTP/1.")) co_return R::Error(http::HttpError(http::HttpErrorKind::UNSUPPORTED_VERSION, string_t(version)).Context("[MessageReader/ReadResponseStreaming]"));

			statusCode = 0;
			const string_t codeText = sp2 == string_t::npos ? text.substr(sp1 + 1) : text.substr(sp1 + 1, sp2 - sp1 - 1);
			if (const auto [ptr, ec] = std::from_chars(codeText.data(), codeText.data() + codeText.size(), statusCode); ec != std::errc{}) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "invalid status code").Context("[MessageReader/ReadResponseStreaming]"));

			reason = sp2 == string_t::npos ? string_t{} : text.substr(sp2 + 1);

			auto parsedHeaders = co_await ReadHeaders(_stopToken);
			if (parsedHeaders.IsError()) co_return R::Error(std::move(parsedHeaders.Error()));

			if (statusCode >= 100 && statusCode < 200) continue;

			headers = std::move(parsedHeaders.Value());
			break;
		}

		if (_sink.onHead && !_sink.onHead(statusCode, reason, headers)) co_return R::Ok(false); // 헤드 단계에서 조기 중단

		// HEAD/204/304 는 본문이 없다 — 프레이밍 헤더를 믿고 읽으면 오지 않을 바이트를 기다린다.
		if (ResponseHasNoBody(statusCode, _requestMethod))
		{
			if (_sink.onBody) (void_t)_sink.onBody({}); // EOF 통지
			co_return R::Ok(true);
		}

		co_return co_await StreamBody(headers, _sink, _stopToken);
	}

	ne::Task<http::HttpResult<bool_t>> MessageReader::StreamBody(const http::Headers& _headers, const http::ResponseCallbacks& _sink, std::stop_token _stopToken)
	{
		using R = http::HttpResult<bool_t>;

		if (const auto transferEncoding = _headers.Get("Transfer-Encoding"); transferEncoding && ne::util::StringFormat::EqualCaseInsensitive(string_view_t(*transferEncoding), string_view_t("chunked"))) co_return co_await StreamChunkedBody(_sink, _stopToken);

		if (const auto contentLength = _headers.Get("Content-Length"))
		{
			std::size_t length = 0;
			if (const auto [ptr, ec] = std::from_chars(contentLength->data(), contentLength->data() + contentLength->size(), length); ec != std::errc{}) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "invalid Content-Length").Context("[MessageReader/StreamBody]"));

			co_return co_await StreamFixedBody(length, _sink, _stopToken);
		}

		co_return R::Ok(true); // 본문 없음
	}

	ne::Task<http::HttpResult<bool_t>> MessageReader::StreamFixedBody(const std::size_t _length, const http::ResponseCallbacks& _sink, std::stop_token _stopToken)
	{
		using R = http::HttpResult<bool_t>;

		std::size_t remaining = _length;
		while (remaining > 0)
		{
			if (dataStart == dataEnd)
			{
				if (auto filled = co_await Fill(_stopToken); filled.IsError()) co_return R::Error(std::move(filled.Error()));
			}

			const std::size_t available = std::min(remaining, dataEnd - dataStart);
			if (_sink.onBody && !_sink.onBody(std::span<const byte_t>(buffer.data() + dataStart, available))) co_return R::Ok(false); // 조기 중단 — 스트림은 본문 중간
			dataStart += available;
			remaining -= available;
		}

		co_return R::Ok(true);
	}

	ne::Task<http::HttpResult<bool_t>> MessageReader::StreamChunkedBody(const http::ResponseCallbacks& _sink, std::stop_token _stopToken)
	{
		using R = http::HttpResult<bool_t>;

		while (true)
		{
			auto sizeLine = co_await ReadLine(_stopToken);
			if (sizeLine.IsError()) co_return R::Error(std::move(sizeLine.Error()));

			string_t sizeText = sizeLine.Value();
			if (const auto ext = sizeText.find(';'); ext != string_t::npos) sizeText = sizeText.substr(0, ext);

			std::size_t chunkSize = 0;
			if (const auto [ptr, ec] = std::from_chars(sizeText.data(), sizeText.data() + sizeText.size(), chunkSize, 16); ec != std::errc{}) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "invalid chunk size").Context("[MessageReader/StreamChunkedBody]"));

			if (chunkSize == 0)
			{
				// 트레일러 헤더(있다면)를 읽어 버린다 — 빈 줄까지 소비.
				while (true)
				{
					auto trailer = co_await ReadLine(_stopToken);
					if (trailer.IsError()) co_return R::Error(std::move(trailer.Error()));
					if (trailer.Value().empty()) break;
				}

				co_return R::Ok(true);
			}

			std::size_t remaining = chunkSize;
			while (remaining > 0)
			{
				if (dataStart == dataEnd)
				{
					if (auto filled = co_await Fill(_stopToken); filled.IsError()) co_return R::Error(std::move(filled.Error()));
				}

				const std::size_t available = std::min(remaining, dataEnd - dataStart);
				if (_sink.onBody && !_sink.onBody(std::span<const byte_t>(buffer.data() + dataStart, available))) co_return R::Ok(false); // 조기 중단
				dataStart += available;
				remaining -= available;
			}

			// 청크 데이터 뒤의 CRLF 를 소비한다.
			auto crlf = co_await ReadLine(_stopToken);
			if (crlf.IsError()) co_return R::Error(std::move(crlf.Error()));
			if (!crlf.Value().empty()) co_return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "chunk data not followed by CRLF").Context("[MessageReader/StreamChunkedBody]"));
		}
	}



	string_t SerializeRequestLine(const http::Request& _request) { return std::format("{} {} HTTP/1.1\r\n", http::ToString(_request.method), _request.target); }

	string_t SerializeStatusLine(const http::Response& _response)
	{
		const string_view_t reason = _response.reason.empty() ? http::DefaultReasonPhrase(_response.statusCode) : string_view_t(_response.reason);
		return std::format("HTTP/1.1 {} {}\r\n", _response.statusCode, reason);
	}

	string_t SerializeHeaders(const http::Headers& _headers)
	{
		string_t result;
		for (const auto& [name, value] : _headers) result += std::format("{}: {}\r\n", name, value);
		result += "\r\n";

		return result;
	}

	http::HttpResult<string_t> BuildRequestHead(const http::Request& _request)
	{
		using R = http::HttpResult<string_t>;

		if (!http::IsInjectionSafe(_request.target) || !http::HeadersAreInjectionSafe(_request.headers)) return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "request target or header contains CR/LF").Context("[Http1/BuildRequestHead]"));

		return R::Ok(SerializeRequestLine(_request) + SerializeHeaders(_request.headers));
	}

	http::HttpResult<string_t> BuildResponseHead(const http::Response& _response)
	{
		using R = http::HttpResult<string_t>;

		if (!http::IsInjectionSafe(_response.reason) || !http::HeadersAreInjectionSafe(_response.headers)) return R::Error(http::HttpError(http::HttpErrorKind::MALFORMED_MESSAGE, "response reason or header contains CR/LF").Context("[Http1/BuildResponseHead]"));

		return R::Ok(SerializeStatusLine(_response) + SerializeHeaders(_response.headers));
	}
}
