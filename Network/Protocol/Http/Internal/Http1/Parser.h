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

		// 1xx(정보) 응답을 무한히 흘려보내 클라이언트를 붙잡아 두는 것을 막는 상한. 실사용에서 최종
		// 응답 전에 오는 1xx 는 한두 개다.
		static constexpr int_t MaxInformationalResponses = 8;

	private:
		ne::network::IStream* stream;
		http::Limits limits;
		std::vector<byte_t> buffer;
		std::size_t dataStart{ 0 }; // buffer 중 아직 소비하지 않은 구간의 시작
		std::size_t dataEnd{ 0 };   // buffer 중 유효 데이터의 끝

	public:
		[[nodiscard]] ne::Task<http::HttpResult<http::Request>> ReadRequest(std::stop_token _stopToken = {});

		/**
		 * @brief 응답 하나를 읽는다.
		 *
		 * @param _requestMethod 이 응답을 유발한 요청의 메서드. **반드시 필요하다** — RFC 9112 §6.3 상
		 * HEAD 응답은 Content-Length 가 있어도 본문이 없다. 이 정보 없이 CL 을 믿고 읽으면 실제 서버
		 * 대부분에서 타임아웃까지 멈춘다(과거 http::Head(url).SendSync() 가 그랬다).
		 *
		 * @note 1xx(정보) 응답은 최종 응답이 아니므로 건너뛴다 — Cloudflare 계열은 요청하지 않아도
		 * 103 Early Hints 를 보낸다.
		 */
		[[nodiscard]] ne::Task<http::HttpResult<http::Response>> ReadResponse(http::Method _requestMethod = http::Method::GET, std::stop_token _stopToken = {});

		// 상태줄+헤더를 읽어 _sink.onHead 를, 본문을 조각마다 _sink.onBody 로 흘려 준다(전체 버퍼링 없음).
		// 반환값: 본문을 끝까지 소비했으면 true(스트림이 메시지 경계 — 재사용 가능), 콜백이 false 로
		// 조기 중단했으면 false(스트림이 본문 중간 — 재사용 불가). 전송/형식 오류는 Error.
		[[nodiscard]] ne::Task<http::HttpResult<bool_t>> ReadResponseStreaming(const http::ResponseCallbacks& _sink, http::Method _requestMethod = http::Method::GET, std::stop_token _stopToken = {});

	public:
		/**
		 * @brief 이 응답을 보낸 뒤 서버가 연결을 닫을 것인지 판정한다.
		 *
		 * "길이 프레이밍이 없으면 EOF 까지 본문" 규칙(RFC 9112 §6.3 item 8)을 적용할 수 있는지의 기준이다.
		 * keep-alive 응답에 그 규칙을 쓰면 무한 대기가 된다.
		 */
		[[nodiscard]] static bool_t ResponseWillClose(const http::Headers& _headers, string_view_t _version);

		/** @brief 상태코드/요청 메서드만으로 "본문이 있을 수 없는 응답" 인지 판정한다(RFC 9112 §6.3). */
		[[nodiscard]] static bool_t ResponseHasNoBody(int_t _statusCode, http::Method _requestMethod) noexcept
		{
			if (_requestMethod == http::Method::HEAD) return true;

			return _statusCode == 204 || _statusCode == 304 || (_statusCode >= 100 && _statusCode < 200);
		}

	private:
		// 스트림에서 더 읽어 buffer 뒤에 채운다. 상대가 EOF 로 닫으면 CONNECTION_CLOSED 를 반환한다.
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> Fill(std::stop_token _stopToken);

		// dataStart 이후 구간에서 "\r\n" 의 절대 offset 을 찾는다(없으면 nullopt).
		[[nodiscard]] std::optional<std::size_t> FindCrlf() const noexcept;

		// 시작 라인 하나(요청 라인 또는 상태 라인)를 읽어 반환하고 소비한다.
		[[nodiscard]] ne::Task<http::HttpResult<string_t>> ReadLine(std::stop_token _stopToken);

		// 빈 줄(CRLF 만 있는 줄)이 나올 때까지 "Name: Value" 라인들을 읽어 Headers 로 파싱한다.
		[[nodiscard]] ne::Task<http::HttpResult<http::Headers>> ReadHeaders(std::stop_token _stopToken);

		// 응답 하나(1xx 포함)를 그대로 읽는다. ReadResponse 가 1xx 를 걸러내며 반복 호출한다.
		[[nodiscard]] ne::Task<http::HttpResult<http::Response>> ReadOneResponse(http::Method _requestMethod, std::stop_token _stopToken);

		/**
		 * @brief Content-Length 값을 엄격하게 파싱한다(버퍼링/스트리밍 두 경로 공용).
		 *
		 * 한 곳에 모은 이유: 예전에는 버퍼링 경로만 엄격했고 스트리밍 경로는 접두 숫자만 읽어 "5abc" 를
		 * 통과시켰다. 같은 공개 API 를 지탱하는 두 경로의 보안 강도가 달라선 안 된다.
		 *
		 * @return 유효하면 길이, 형식 위반/중복 불일치/상한 초과면 에러.
		 */
		[[nodiscard]] http::HttpResult<std::size_t> ParseContentLength(const http::Headers& _headers) const;

		/**
		 * @brief Content-Length / Transfer-Encoding: chunked 를 보고 본문을 읽는다.
		 * @param _canReadUntilClose 둘 다 없을 때 EOF 까지 읽을지 여부. **응답에서만** true 다 — RFC 9112
		 * §6.3 item 8. 요청에서 프레이밍이 없으면 본문이 없다는 뜻이므로 false 여야 한다(EOF 까지
		 * 기다리면 keep-alive 연결이 멈춘다).
		 */
		[[nodiscard]] ne::Task<http::HttpResult<http::Body>> ReadBody(const http::Headers& _headers, bool_t _canReadUntilClose, std::stop_token _stopToken);
		[[nodiscard]] ne::Task<http::HttpResult<http::Body>> ReadChunkedBody(std::stop_token _stopToken);
		// 상대가 연결을 닫을 때까지 읽어 남은 전부를 본문으로 삼는다(길이 프레이밍이 없는 응답).
		[[nodiscard]] ne::Task<http::HttpResult<http::Body>> ReadBodyUntilClose(std::stop_token _stopToken);

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
