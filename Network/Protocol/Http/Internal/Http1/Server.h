//
// Created by hscloud on 26. 7. 20.
//

#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <stop_token>
#include <utility>
#include "Base/Coroutine/Task.h"
#include "Io/Context.h"
#include "Io/Socket.h"
#include "Network/Stream/Tls/TlsStream.h"
#include "Network/Protocol/Http/Message/Request.h"
#include "Network/Protocol/Http/Message/Response.h"
#include "Network/Protocol/Http/Diagnostic/Error.h"
#include "Network/Protocol/Http/Limits.h"

namespace ne { class Event; }

namespace ne::network::http_1::internal
{
	/**
	 * @class Server
	 * @brief HTTP/1.1 서버 엔진입니다(내부). 공개 진입점은 ServerBuilder 이며, 그 라우팅 핸들러를 이 엔진이 구동합니다.
	 *
	 * _tlsConfig 를 넘기면 TlsStream::Accept 로, 아니면 PlainStream 으로 각 연결을 감쌉니다 — Handler 는
	 * 평문/TLS 여부를 몰라도 됩니다. keep-alive 로 한 연결에서 여러 요청을 순차 처리합니다.
	 *
	 * @note 소켓/리스너는 값으로 받아 코루틴 프레임이 소유합니다(수명 안전). Serve() 는 각 연결을
	 * 독립 태스크로 동시 처리하며, 종료 시 진행 중인 연결을 모두 취소·완료시킨 뒤 반환합니다.
	 */
	class Server
	{
	public:
		using Handler = std::function<ne::Task<http::HttpResult<http::Response>>(const http::Request&)>;

	public:
		explicit Server(Handler _handler, const TlsConfig* _tlsConfig = nullptr, const http::Limits _limits = {}) noexcept
			: handler(std::move(_handler))
			, tlsConfig(_tlsConfig)
			, limits(_limits) {}

	private:
		Handler handler;
		const TlsConfig* tlsConfig;
		http::Limits limits;

	public:
		/** @brief 이미 Accept() 된 소켓 하나에서 요청 1개를 읽어 Handler 를 호출하고, 응답을 보낸 뒤 연결을 닫습니다(단발). */
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> HandleOne(ne::io::Socket _socket, ne::io::Context& _context, std::stop_token _stopToken = {}) const;

		/**
		 * @brief 이미 Accept() 된 소켓 하나를 keep-alive 로 처리합니다 — 요청이 `Connection: close` 를
		 * 보내거나 피어가 연결을 닫을(EOF) 때까지 요청/응답을 반복하고, 그 뒤 연결을 닫습니다.
		 */
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> HandleConnection(ne::io::Socket _socket, ne::io::Context& _context, std::stop_token _stopToken = {}) const;

		/**
		 * @brief 이미 수립된 스트림(평문 또는 TLS 핸드셰이크 완료) 하나를 keep-alive 로 처리합니다.
		 * @note TLS accept 를 바깥(예: ALPN 결과로 버전을 고르는 통합 서버)에서 마친 경우의 진입점 —
		 *       이 경로에서는 생성자의 _tlsConfig 를 사용하지 않습니다.
		 */
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> HandleEstablished(std::unique_ptr<IStream> _stream, std::stop_token _stopToken = {}) const;

		/**
		 * @brief _listener 에서 연결을 계속 accept 하며 각 연결을 HandleConnection() 의 독립 태스크로 동시 처리합니다.
		 * @note _stopToken 취소 또는 Accept 실패 시 진행 중인 연결의 I/O 를 일괄 취소하고, 전부 끝난 뒤 반환합니다.
		 */
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> Serve(ne::io::Socket _listener, ne::io::Context& _context, std::stop_token _stopToken = {}) const;

	private:
		// 연결 하나를 배경 태스크로 처리하고, 마지막 활성 연결이 끝나면 _allDone 을 신호한다(Serve 의 drain 용).
		[[nodiscard]] ne::Task<void_t> RunConnection(ne::io::Socket _socket, ne::io::Context& _context, std::stop_token _stopToken, std::size_t& _active, ne::Event& _allDone) const;
	};
}
