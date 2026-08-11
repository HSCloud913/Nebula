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
	 * 이미 수립된 스트림(평문 또는 TLS 핸드셰이크 완료) 하나에서 keep-alive 로 요청/응답을 반복하는
	 * 것만 담당합니다 — accept 루프와 TLS 수립은 ServerBuilder::Serve 의 몫입니다. 그래서 Handler 는
	 * 평문/TLS 여부를 몰라도 됩니다.
	 *
	 * @note 예전에는 자체 accept 루프(Serve/RunConnection/HandleConnection/HandleOne)와 TlsConfig 를
	 * 함께 들고 있었지만, ServerBuilder 가 그 전부를 자기 쪽에 다시 구현해 쓰고 있었고 이쪽은 호출처가
	 * 없었습니다. "리스닝 소켓을 구동하는 두 가지 방법" 을 없애기 위해 이 절반을 삭제했습니다.
	 */
	class Server
	{
	public:
		using Handler = std::function<ne::Task<http::HttpResult<http::Response>>(const http::Request&)>;

	public:
		explicit Server(Handler _handler, const http::Limits _limits = {}) noexcept
			: handler(std::move(_handler))
			, limits(_limits) {}

	private:
		Handler handler;
		http::Limits limits;

	public:
		/** @brief 이미 수립된 스트림 하나를 keep-alive 로 처리합니다 — `Connection: close` 또는 EOF 까지 반복합니다. */
		[[nodiscard]] ne::Task<http::HttpResult<void_t>> HandleEstablished(std::unique_ptr<IStream> _stream, std::stop_token _stopToken = {}) const;
	};
}
