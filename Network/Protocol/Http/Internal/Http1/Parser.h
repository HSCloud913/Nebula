//
// Created by hscloud on 26. 7. 20.
//

#pragma once
#include <cstddef>
#include <optional>
#include <stop_token>
#include <vector>
#include "Base/Type.h"
#include "Base/Coroutine/Task.h"
#include "Network/Stream/IStream.h"
#include "Network/Protocol/Http/Message/Request.h"
#include "Network/Protocol/Http/Message/Response.h"
#include "Network/Protocol/Http/Diagnostic/Error.h"
#include "Network/Protocol/Http/Limits.h"
#include "Network/Protocol/Http/ResponseCallbacks.h"

namespace ne::network::http_1::internal
{
	/**
	 * @class MessageReader
	 * @brief IStream 에서 HTTP/1.1 요청/응답 메시지 하나를 증분(incremental) 파싱하는 내부 리더입니다.
	 *
	 * Client/Server 내부에서만 쓰이는 저수준 컴포넌트입니다. 시작 라인(요청/상태 라인) → 헤더 →
	 * 본문(Content-Length 또는 Transfer-Encoding: chunked) 순으로 스트림을 필요한 만큼 반복해서
	 * Receive() 하며, 메시지 경계 이후 버퍼에 남은 바이트는 버리지 않고 보존됩니다 — 연결당 리더
	 * 하나를 재사용하면 keep-alive 연결에서 다음 메시지를 이어서 읽을 수 있습니다.
	 *
	 * @note 헤더 블록과 본문 크기에는 상한(http::Limits — 기본 8KB/64MB)이 있어, 신뢰할 수 없는
	 * 피어가 무한정 큰 메시지를 보내 메모리를 고갈시키는 것을 막습니다.
	 */
	class MessageReader
	{
	public:
		explicit MessageReader(ne::network::IStream& _stream, const http::Limits _limits = {}) noexcept
			: stream(&_stream)
			, limits(_limits) {}

	private:
		static constexpr std::size_t ReadChunkSize = 8 * 1024;

	private:
		ne::network::IStream* stream;
		http::Limits limits;
		std::vector<byte_t> buffer;
		std::size_t dataStart{ 0 }; // buffer 중 아직 소비하지 않은 구간의 시작
		std::size_t dataEnd{ 0 };   // buffer 중 유효 데이터의 끝

	public:
		[[nodiscard]] ne::Task<http::HttpResult<http::Request>> ReadRequest(std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<http::HttpResult<http::Response>> ReadResponse(std::stop_token _stopToken = {});

		// 상태줄+헤더를 읽어 _sink.onHead 를, 본문을 조각마다 _sink.onBody 로 흘려 준다(전체 버퍼링 없음).
		// 반환값: 본문을 끝까지 소비했으면 true(스트림이 메시지 경계 — 재사용 가능), 콜백이 false 로
		// 조기 중단했으면 false(스트림이 본문 중간 — 재사용 불가). 전송/형식 오류는 Error.
		[[nodiscard]] ne::Task<http::HttpResult<bool_t>> ReadResponseStreaming(const http::ResponseCallbacks& _sink, std::stop_token _stopToken = {});

	private:
		// 스트림에서 더 읽어 buffer 뒤에 채운다. 상대가 EOF 로 닫으면 CONNECTION_CLOSED 를 반환한다.
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> Fill(std::stop_token _stopToken);

		// dataStart 이후 구간에서 "\r\n" 의 절대 offset 을 찾는다(없으면 nullopt).
		[[nodiscard]] std::optional<std::size_t> FindCrlf() const noexcept;

		// 시작 라인 하나(요청 라인 또는 상태 라인)를 읽어 반환하고 소비한다.
		[[nodiscard]] ne::Task<http::HttpResult<string_t>> ReadLine(std::stop_token _stopToken);

		// 빈 줄(CRLF 만 있는 줄)이 나올 때까지 "Name: Value" 라인들을 읽어 Headers 로 파싱한다.
		[[nodiscard]] ne::Task<http::HttpResult<http::Headers>> ReadHeaders(std::stop_token _stopToken);

		// Content-Length 또는 Transfer-Encoding: chunked 헤더를 보고 본문을 읽는다. 둘 다 없으면 빈 본문.
		[[nodiscard]] ne::Task<http::HttpResult<http::Body>> ReadBody(const http::Headers& _headers, std::stop_token _stopToken);
		[[nodiscard]] ne::Task<http::HttpResult<http::Body>> ReadChunkedBody(std::stop_token _stopToken);

		// 정확히 _length 바이트를 읽어 반환한다(버퍼에 이미 있는 만큼 재사용 + 부족하면 Fill() 반복).
		[[nodiscard]] ne::Task<http::HttpResult<std::vector<byte_t>>> ReadExact(std::size_t _length, std::stop_token _stopToken);

		// 본문을 조각째 _sink.onBody 로 흘려 준다(Content-Length/chunked/무본문 분기). 반환값은 ReadResponseStreaming 참조.
		[[nodiscard]] ne::Task<http::HttpResult<bool_t>> StreamBody(const http::Headers& _headers, const http::ResponseCallbacks& _sink, std::stop_token _stopToken);
		[[nodiscard]] ne::Task<http::HttpResult<bool_t>> StreamFixedBody(std::size_t _length, const http::ResponseCallbacks& _sink, std::stop_token _stopToken);
		[[nodiscard]] ne::Task<http::HttpResult<bool_t>> StreamChunkedBody(const http::ResponseCallbacks& _sink, std::stop_token _stopToken);
	};

	// 요청 라인/상태 라인과 헤더 블록을 직렬화한다. Client/Server 가 전송 직전에 사용한다.
	[[nodiscard]] string_t SerializeRequestLine(const http::Request& _request);
	[[nodiscard]] string_t SerializeStatusLine(const http::Response& _response);
	[[nodiscard]] string_t SerializeHeaders(const http::Headers& _headers);

	// 위 직렬화에 CR/LF 인젝션 검증을 얹은 전송용 헤드 빌더. target/reason·헤더에 raw CR/LF 가 있으면
	// 바이트를 만들지 않고 MALFORMED_MESSAGE 로 실패한다(헤더 인젝션/요청 스머글링 차단).
	[[nodiscard]] http::HttpResult<string_t> BuildRequestHead(const http::Request& _request);
	[[nodiscard]] http::HttpResult<string_t> BuildResponseHead(const http::Response& _response);
}
