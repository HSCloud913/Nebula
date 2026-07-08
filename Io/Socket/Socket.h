//
// Created by hscloud on 26. 7. 8.
//
// Level 3 — 코루틴 기반 비동기 소켓(데이터 경로). Receive/Send 는 co_await 에서 suspend 되고
// 완료 시 IoContext 루프가 재개한다. 값 기반(규칙 4): 소켓 핸들을 소유하는 move-only 리소스.
// (Accept/Connect 는 엔진 AcceptEx/ConnectEx 확장 후 Phase 5b 에서 추가)

#pragma once
#include <cstddef>
#include <span>
#include "Type.h"
#include "IoType.h"
#include "Handle.h"
#include "IoResult.h"
#include "Coroutine/Task.h"

BEGIN_NS(ne::io)
	class IoContext;

	// 연결 대상 — 숫자 IP + 포트(값 기반). 호스트명 DNS 해석은 상위 계층 책임.
	struct Endpoint
	{
		string_view_t ip;
		uint16_t      port{ 0 };
	};

	namespace detail
	{
		// 플랫폼 close 를 .cpp 로 숨긴다(소켓 핸들 deleter).
		void_t CloseSocketHandle(socket_t _handle) noexcept;

		struct SocketHandleDeleter
		{
			void_t operator()(const socket_t _handle) const noexcept { CloseSocketHandle(_handle); }
		};
	}

	class Socket
	{
	public:
		NEBULA_NON_COPYABLE(Socket)
		NEBULA_DEFAULT_MOVE(Socket)

		~Socket() = default;

	private:
#if defined(_WIN32)
		using SocketHandle = ne::Handle<socket_t, detail::SocketHandleDeleter, INVALID_SOCKET>;
#elif defined(IS_POSIX)
		using SocketHandle = ne::Handle<socket_t, detail::SocketHandleDeleter, -1>;
#endif

		Socket(SocketHandle&& _handle, IoContext& _context) noexcept;

	private:
		SocketHandle handle;
		IoContext*   context;

	public:
		// 이미 존재하는 소켓(소켓 쌍, 저수준 listen 소켓 등)을 감싼다(소유권 이전).
		[[nodiscard]] static IoResult<Socket> Adopt(IoContext& _context, socket_t _handle);

		// _endpoint 로 소켓을 새로 만들어 비동기 연결(ConnectEx)하고 연결된 Socket 을 돌려준다.
		[[nodiscard]] static ne::Task<IoResult<Socket>> Connect(IoContext& _context, Endpoint _endpoint);

		// 이 listen 소켓에서 연결 하나를 비동기 수락(AcceptEx)해 새 Socket 을 돌려준다.
		[[nodiscard]] ne::Task<IoResult<Socket>> Accept();

		[[nodiscard]] ne::Task<IoResult<std::size_t>> Receive(std::span<ne::byte_t> _buffer);
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Send(std::span<const ne::byte_t> _buffer);

		[[nodiscard]] ne::Result<void_t, IoError> Close();

	public:
		[[nodiscard]] socket_t Handle() const noexcept { return handle.Get(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
	};

END_NS
