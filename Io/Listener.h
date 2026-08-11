//
// Created by hscloud on 26. 7. 24.
//

#pragma once
#include <stop_token>
#include <utility>
#include "Base/Type.h"
#include "Base/Coroutine/Task.h"
#include "Diagnostic/Type.h"
#include "Io/Diagnostic/Error.h"
#include "Io/Socket.h"

namespace ne::io
{
	class Context;

	/**
	 * @class Listener
	 * @brief 서버측 수신 소켓을 감싸는 accept 헬퍼입니다.
	 *
	 * Bind() 팩토리가 소켓 생성·bind·listen 을 한 번에 끝내 "듣고 있는 소켓" 하나를 돌려주고,
	 * Accept() 코루틴으로 연결을 하나씩 수락합니다. 주소 문자열에서 IPv4/IPv6 계열을 자동 판별하며,
	 * 포트 0 으로 bind 하면 LocalPort() 로 커널이 배정한 실제 포트를 조회할 수 있습니다(테스트 편의).
	 *
	 * @note 동시성(연결마다 코루틴 분기)은 만들지 않습니다 — 호출자가 Accept() 로 받은 소켓을
	 *       원하는 방식으로 처리하면 됩니다. move 가능, copy 불가.
	 */
	class Listener
	{
	private:
		explicit Listener(Socket&& _socket) noexcept
			: socket(std::move(_socket)) {}

	public:
		~Listener() = default;

		NEBULA_NON_COPYABLE(Listener)
		NEBULA_DEFAULT_MOVE(Listener)

	private:
		Socket socket;

	public:
		/** @brief 다음 연결을 수락해 연결된 Socket 을 반환합니다. _stopToken 취소 시 실패로 끝납니다. */
		[[nodiscard]] ne::Task<IoResult<Socket>> Accept(std::stop_token _stopToken = {}) { return socket.Accept(false, std::move(_stopToken)); }

		/** @brief bind 된 로컬 포트를 호스트 바이트 순서로 반환합니다. 조회 실패 시 0. */
		[[nodiscard]] uint16_t LocalPort() const noexcept;

		[[nodiscard]] Socket& ListenerSocket() noexcept { return socket; }

	public:
		/**
		 * @brief _ip:_port 에 bind + listen 한 리스너를 만듭니다.
		 * @param _ip 바인딩 주소("0.0.0.0"/"127.0.0.1"/"::"). 계열은 문자열에서 자동 판별합니다.
		 * @param _port 포트. 0 이면 커널이 임시 포트를 배정하며 LocalPort() 로 확인합니다.
		 * @param _backlog listen 대기 큐 길이.
		 */
		[[nodiscard]] static IoResult<Listener> Bind(Context& _context, string_view_t _ip, uint16_t _port, int_t _backlog = SOMAXCONN);
	};
}
