//
// Created by hscloud on 26. 7. 8.
//
// Level 3 — 코루틴 기반 비동기 소켓(데이터 경로). Receive/Send 는 co_await 에서 suspend 되고
// 완료 시 Context 루프가 재개한다. 값 기반(규칙 4): 소켓 핸들을 소유하는 move-only 리소스.
// (Accept/Connect 는 엔진 AcceptEx/ConnectEx 확장 후 Phase 5b 에서 추가)

#pragma once
#include <cstddef>
#include <span>
#include <stop_token>
#include "Base/Type.h"
#include "Io/IoType.h"
#include "Base/Handle.h"
#include "Io/IoResult.h"
#include "Base/Coroutine/Task.h"
#include "Io/Buffer/BufferChain.h"
#include "Io/Engine/IRegisteredBufferProvider.h"

BEGIN_NS(ne::io)
	class Context;

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
		// socket() 만 수행한다(bind/listen/connect 없음) — Bind() 전에 setsockopt(SO_REUSEADDR 등)를
		// 끼워넣을 틈을 주기 위해 생성만 분리했다. Windows 는 Connect 와 동일하게 항상
		// WSA_FLAG_REGISTERED_IO 로 만든다.
		[[nodiscard]] static IoResult<Socket> Create(Context& _context, int_t _family, int_t _type = SOCK_STREAM, int_t _protocol = IPPROTO_TCP);

		// 이미 존재하는 소켓(소켓 쌍, 저수준 listen 소켓 등)을 감싼다(소유권 이전).
		[[nodiscard]] static IoResult<Socket> Attach(socket_t _handle, Context& _context);

	public:
		// 이 소켓에 bind() 한다(listen 은 안 함). UDP 서버는 이거 하나로 끝(그 뒤 SendTo/ReceiveFrom).
		// 숫자 IP 전용(DNS 없음) — Connect 와 동일한 원칙. bind()는 논블로킹 동기 syscall이라 Task 가
		// 아니다.
		[[nodiscard]] ne::Result<void_t, IoError> Bind(string_view_t _ip, uint16_t _port);

		// 이 소켓에 listen() 한다(Bind() 가 먼저 호출돼 있어야 함) — 이후 Accept() 호출 가능.
		[[nodiscard]] ne::Result<void_t, IoError> Listen(int_t _backlog = SOMAXCONN);

		// 이 listen 소켓에서 연결 하나를 비동기 수락(AcceptEx)해 새 Socket 을 돌려준다. Create()와
		// 동일하게 accept 소켓도 항상 WSA_FLAG_REGISTERED_IO 로 생성된다.
		// _stopToken 은 Connect 와 동일 계약(진행 중 AcceptEx 커널 취소).
		[[nodiscard]] ne::Task<IoResult<Socket>> Accept(std::stop_token _stopToken = {});


		// 이 소켓으로 비동기 연결(ConnectEx)한다 — Create()/Attach() 로 이미 만들어진 소켓 위에서
		// 동작한다(Bind() 로 소스 포트를 먼저 고정해두는 것도 가능 — 엔진이 미리 bind 안 됐으면
		// any:0 으로 자동 bind 하고, 이미 bind 돼 있으면 그 시도는 조용히 무시된다). OpCode::Connect
		// 는 엔진에서 ConnectEx(Windows, connection-oriented 소켓 전제)로 처리되므로 비TCP 계열은
		// 엔진이 실패를 값으로 돌려줄 수 있다(예외 없음). _stopToken 이 stop 되면 진행 중인 ConnectEx
		// 를 커널 취소한다(when_any/Timeout 콤비네이터가 타임아웃 경합에서 쓴다) — 기본값(빈 토큰)은
		// 취소 없음.
		[[nodiscard]] ne::Task<ne::Result<void_t, IoError>> Connect(string_view_t _ip, uint16_t _port = 0, std::stop_token _stopToken = {});

		[[nodiscard]] ne::Result<void_t, IoError> Close();

	public:
		// _stopToken: stop 되면 진행 중인 op 를 커널 취소(CancelIoEx 등)한다 — Io::Awaitable 이 이미
		// 갖고 있는 계약을 그대로 노출한 것. 기본값(빈 토큰)은 지금까지와 동일하게 취소 없음.
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Receive(std::span<ne::byte_t> _buffer, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Send(std::span<const ne::byte_t> _buffer, std::stop_token _stopToken = {});

		// scatter/gather — 여러 세그먼트를 한 요청으로 송수신한다(WSASend/WSARecv 멀티 WSABUF,
		// sendmsg/recvmsg). _chain 은 완료까지 호출자가 살려둬야 한다.
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Receivev(const BufferChain& _chain, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Sendv(const BufferChain& _chain, std::stop_token _stopToken = {});

		// 메모리→소켓 zero-copy 송신 — RegisteredBuffer/BufferPool 로 사전 등록된 영역 내부의 포인터와
		// 그 handle 을 넘긴다(_buffer 는 그 등록 영역의 sub-range 여야 한다). 엔진이 SendMemZeroCopy
		// (RIO/MSG_ZEROCOPY)를 지원하지 않으면 IoError(UNSUPPORTED). Windows(RIO)는 Create()/Accept()가
		// 항상 WSA_FLAG_REGISTERED_IO 로 소켓을 만들어두므로 별도 준비 없이 바로 쓸 수 있다.
		// 주의: Windows RIO 경로는 IocpEngine::Cancel() 이 아직 추적하지 않는다(RIO 제출 취소는
		// 범위 밖) — _stopToken 을 줘도 이 op 은 커널 취소되지 않고 완료까지 그대로 진행된다.
		// MSG_ZEROCOPY(POSIX)는 일반 op 과 동일하게 취소된다.
		[[nodiscard]] ne::Task<IoResult<std::size_t>> SendZeroCopy(BufferHandle _handle, std::span<const ne::byte_t> _buffer, std::stop_token _stopToken = {});

		// 파일→소켓 zero-copy 전송(TransmitFile/sendfile). _file 은 완료까지 호출자가 열어둬야 한다.
		[[nodiscard]] ne::Task<IoResult<std::size_t>> SendFile(file_t _file, ulonglong_t _offset, std::size_t _length, std::stop_token _stopToken = {});

		// 비연결형(UDP 등) 송수신 — Connect 없이 매 호출마다 목적지를 지정하거나(SendTo) 발신자
		// 주소를 돌려받는다(ReceiveFrom). 연결된 소켓(Connect/Accept 로 만든)에도 쓸 수 있지만
		// 보통 SOCK_DGRAM 소켓(Socket::Create(..., SOCK_DGRAM, IPPROTO_UDP) 으로 만들고 Connect 는
		// 안 부르는 편이 자연스럽다)과 함께 쓴다.
		[[nodiscard]] ne::Task<IoResult<std::size_t>> SendTo(std::span<const ne::byte_t> _buffer, string_view_t _ip, uint16_t _port = 0, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> ReceiveFrom(std::span<ne::byte_t> _buffer, string_t& _ip, uint16_t& _port, std::stop_token _stopToken = {});

	public:
		// SO_REUSEADDR — 재시작 시 "Address already in use" 방지. Create() 후 Bind() 전에 호출할 것
		// (bind() 이후엔 적용 안 됨). Windows/POSIX 의미가 미묘하게 다르니(Windows 는 더 permissive)
		// 호출자가 그 차이를 알고 쓴다는 전제.
		[[nodiscard]] ne::Result<void_t, IoError> SetReuseAddress(bool_t _enable);
		// TCP_NODELAY — Nagle 알고리즘 비활성화(저지연 우선). 아무 때나 호출 가능.
		[[nodiscard]] ne::Result<void_t, IoError> SetNoDelay(bool_t _enable);

	public:
		[[nodiscard]] socket_t Handle() const noexcept { return handle.Get(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
	};

END_NS
