//
// Created by hscloud on 26. 7. 8.
//
// Level 3 — 코루틴 기반 비동기 소켓(데이터 경로). Receive/Send 는 co_await 에서 suspend 되고
// 완료 시 Context 루프가 재개한다. 값 기반(규칙 4): 소켓 핸들을 소유하는 move-only 리소스.
// (Accept/Connect 는 엔진 AcceptEx/ConnectEx 확장 후 Phase 5b 에서 추가)

#pragma once
#include <cstddef>
#include <span>
#include "Base/Type.h"
#include "Io/IoType.h"
#include "Base/Handle.h"
#include "Io/IoResult.h"
#include "Base/Coroutine/Task.h"
#include "Io/Buffer/BufferChain.h"

BEGIN_NS(ne::io)
	class Context;

	// ReceiveFrom 이 돌려주는 발신자 주소 — 입력측(ip 는 string_view)과 달리 sockaddr_storage 수명이
	// 이미 끝난 뒤에도 값으로 들고 있어야 하므로 ip 를 string_t 로 소유한다(view 아님).
	struct ReceiveFromResult
	{
		std::size_t length{ 0 };
		string_t ip;
		uint16_t port{ 0 };
	};

	class Socket
	{
	private:
#if defined(_WIN32)
		using SocketHandle = ne::Handle<socket_t, decltype([](const socket_t _handle) { ::closesocket(_handle); }), INVALID_SOCKET>;
#elif defined(IS_POSIX)
		using SocketHandle = ne::Handle<socket_t, decltype([](const socket_t _handle) { ::close(_handle); }), -1>;
#endif

	private:
		Socket(SocketHandle&& _handle, Context& _context) noexcept;

	public:
		~Socket() = default;

		NEBULA_NON_COPYABLE(Socket)
		NEBULA_DEFAULT_MOVE(Socket)

	private:
		SocketHandle handle;
		Context* context;

	public:
		// 이미 존재하는 소켓(소켓 쌍, 저수준 listen 소켓 등)을 감싼다(소유권 이전).
		[[nodiscard]] static IoResult<Socket> Attach(socket_t _handle, Context& _context);

		// _endpoint 로 소켓을 새로 만들어 비동기 연결(ConnectEx)하고 연결된 Socket 을 돌려준다.
		// _type/_protocol 기본값(SOCK_STREAM/IPPROTO_TCP)은 기존 동작과 동일 — SOCK_DGRAM 등을
		// 넘길 수도 있으나, OpCode::Connect 는 엔진에서 ConnectEx(Windows, connection-oriented
		// 소켓 전제)로 처리되므로 비TCP 계열은 엔진이 실패를 값으로 돌려줄 수 있다(예외 없음).
		[[nodiscard]] static ne::Task<IoResult<Socket>> Connect(Context& _context, string_view_t _ip, uint16_t _port = 0, int_t _type = SOCK_STREAM, int_t _protocol = IPPROTO_TCP);

		// 이 listen 소켓에서 연결 하나를 비동기 수락(AcceptEx)해 새 Socket 을 돌려준다.
		[[nodiscard]] ne::Task<IoResult<Socket>> Accept();

		[[nodiscard]] ne::Task<IoResult<std::size_t>> Receive(std::span<ne::byte_t> _buffer);
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Send(std::span<const ne::byte_t> _buffer);

		// scatter/gather — 여러 세그먼트를 한 요청으로 송수신한다(WSASend/WSARecv 멀티 WSABUF,
		// sendmsg/recvmsg). _chain 은 완료까지 호출자가 살려둬야 한다.
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Receivev(const BufferChain& _chain);
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Sendv(const BufferChain& _chain);

		// 비연결형(UDP 등) 송수신 — Connect 없이 매 호출마다 목적지를 지정하거나(SendTo) 발신자
		// 주소를 돌려받는다(ReceiveFrom). 연결된 소켓(Connect/Accept 로 만든)에도 쓸 수 있지만
		// 보통 SOCK_DGRAM 소켓(Socket::Connect(..., SOCK_DGRAM, IPPROTO_UDP) 로 만들되 실제로는
		// Connect 를 안 부르고 Adopt 로 감싸 쓰는 편이 자연스럽다)과 함께 쓴다.
		[[nodiscard]] ne::Task<IoResult<std::size_t>> SendTo(std::span<const ne::byte_t> _buffer, string_view_t _ip, uint16_t _port = 0);
		[[nodiscard]] ne::Task<IoResult<std::size_t>> ReceiveFrom(std::span<ne::byte_t> _buffer, string_t& _ip, uint16_t& _port);

		[[nodiscard]] ne::Result<void_t, IoError> Close();

	public:
		[[nodiscard]] socket_t Handle() const noexcept { return handle.Get(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
	};

END_NS
