//
// Created by hscloud on 26. 7. 8.
//

#pragma once
#include <chrono>
#include <cstddef>
#include <span>
#include <stop_token>
#include "Base/Type.h"
#include "Io/Handle.h"
#include "Base/Handle.h"
#include "Io/Diagnostic/Error.h"
#include "Base/Coroutine/Task.h"
#include "Memory/Buffer/BufferChain.h"
#include "Io/Buffer/RegisteredBufferProvider.h"

namespace ne::io
{
	class Context;

	/**
	 * @class SocketAddress
	 * @brief 소켓의 한쪽 끝(IP 문자열 + 포트 + 주소 체계)을 나타내는 값 타입입니다.
	 *
	 * getsockname/getpeername 결과를 사용자가 다루기 쉬운 형태로 정규화합니다 — 액세스 로그,
	 * 레이트 리밋, 차단 목록처럼 "누가 접속했는가" 를 다루는 기능의 입력이 됩니다.
	 *
	 * @note sockaddr 을 그대로 노출하지 않는 이유는 그러면 이 헤더가 <winsock2.h>/<sys/socket.h> 를
	 * 저장소 전반에 퍼뜨리게 되기 때문입니다(Base/WinsockApi.h 의 존재 이유와 같은 문제).
	 */
	struct SocketAddress
	{
		string_t ip;
		uint16_t port{ 0 };
		int_t family{ 0 }; // AF_INET / AF_INET6
	};

	/**
	 * @class Socket
	 * @brief 코루틴 기반 비동기 소켓.
	 *
	 * Receive/Send/Accept/Connect 등은 co_await 지점에서 suspend 되고, 완료 시 Context 의
	 * 루프가 코루틴을 재개한다. OS 소켓 핸들을 소유하는 move-only 리소스이며, RIO(Registered I/O)로
	 * 생성됐는지 여부를 함께 들고 있어 SendZeroCopy 가능 여부를 판별한다. stream/datagram,
	 * scatter-gather, readiness 대기, zero-copy 송신 등 다양한 데이터 경로를 제공한다.
	 */
	class Socket
	{
	private:
#if defined(_WIN32)
		using SocketHandle = ne::Handle<socket_t, decltype([](const socket_t _handle) { ::closesocket(_handle); }), INVALID_SOCKET>;
#elif defined(IS_POSIX)
		using SocketHandle = ne::Handle<socket_t, decltype([](const socket_t _handle) { ::close(_handle); }), -1>;
#endif

	private:
		Socket(SocketHandle&& _handle, Context& _context, bool_t _isRegisteredIo) noexcept;

	public:
		~Socket();

		NEBULA_NON_COPYABLE(Socket)
		Socket(Socket&&) noexcept = default;
		Socket& operator=(Socket&&) noexcept;

	private:
		SocketHandle handle;
		Context* context;
		bool_t isRegisteredIo{ false };

		// 이 소켓 핸들이 닫히기 직전, 엔진의 IOCP 연관 캐시에서 핸들 값을 제거한다(값 재사용 오연관 방지).
		void_t DeregisterFromEngine() noexcept;

	public:
		[[nodiscard]] static IoResult<Socket> Create(Context& _context, int_t _family, int_t _type = SOCK_STREAM, int_t _protocol = IPPROTO_TCP, bool_t _isRegisteredIo = false);

		[[nodiscard]] static IoResult<Socket> Attach(socket_t _handle, Context& _context, bool_t _isRegisteredIo = false);

	public:
		[[nodiscard]] IoResult<void_t> Bind(string_view_t _ip, uint16_t _port);

		[[nodiscard]] IoResult<void_t> Listen(int_t _backlog = SOMAXCONN);

		[[nodiscard]] ne::Task<IoResult<Socket>> Accept(bool_t _isRegisteredIo = false, std::stop_token _stopToken = {});

	public:
		[[nodiscard]] ne::Task<IoResult<void_t>> Connect(string_view_t _ip, uint16_t _port = 0, std::stop_token _stopToken = {});

	public:
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Receive(std::span<ne::byte_t> _buffer, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Send(std::span<const ne::byte_t> _buffer, std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<IoResult<std::size_t>> Receivev(const ne::memory::BufferChain& _chain, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> Sendv(const ne::memory::BufferChain& _chain, std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<IoResult<void_t>> WaitReadable(std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<void_t>> WaitWritable(std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<IoResult<std::size_t>> SendZeroCopy(BufferHandle _handle, std::span<const ne::byte_t> _buffer, std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<IoResult<std::size_t>> SendFile(file_t _file, ulonglong_t _offset, std::size_t _length, std::stop_token _stopToken = {});

		[[nodiscard]] ne::Task<IoResult<std::size_t>> SendTo(std::span<const ne::byte_t> _buffer, string_view_t _ip, uint16_t _port = 0, std::stop_token _stopToken = {});
		[[nodiscard]] ne::Task<IoResult<std::size_t>> ReceiveFrom(std::span<ne::byte_t> _buffer, string_t& _ip, uint16_t& _port, std::stop_token _stopToken = {});

	public:
		/**
		 * @class ShutdownMode
		 * @brief Shutdown() 이 닫을 방향입니다.
		 *
		 * SEND 만 닫는 것이 "half-close" 로, 상대에게 EOF 를 알리면서 응답은 계속 받는 프로토콜
		 * (예: HTTP/1.0 스타일 요청 종료 표시)에 필요합니다.
		 */
		enum class ShutdownMode : uint_t
		{
			SEND,     // 더 보낼 것이 없음을 알린다(상대는 EOF 를 본다). 수신은 계속 가능.
			RECEIVE,  // 더 받지 않는다. 이후 도착 데이터는 버려진다.
			BOTH,     // 양방향 종료.
		};

		/**
		 * @class LingerOption
		 * @brief SO_LINGER 설정값입니다.
		 *
		 * @note isEnabled=true + seconds=0 은 "닫을 때 RST 를 보내고 즉시 버림" 이라는 특수한 의미로,
		 * TIME_WAIT 를 피하려고 흔히 오용됩니다. 전송 중 데이터가 유실되므로 의도한 경우만 쓰세요.
		 */
		struct LingerOption
		{
			bool_t isEnabled{ false };
			uint16_t seconds{ 0 };
		};

	public:
		/** @brief SO_REUSEADDR — TIME_WAIT 상태의 주소에 다시 bind 할 수 있게 합니다. */
		[[nodiscard]] IoResult<void_t> SetReuseAddress(bool_t _isEnabled);
		/** @brief TCP_NODELAY — Nagle 알고리즘을 끕니다(작은 메시지의 지연 감소). */
		[[nodiscard]] IoResult<void_t> SetNoDelay(bool_t _isEnabled);

		/**
		 * @brief SO_REUSEPORT — 여러 소켓이 같은 주소/포트에 bind 해 커널이 연결을 분산하게 합니다.
		 * @note Linux/BSD 전용입니다. Windows 에는 대응 옵션이 없어 UNSUPPORTED 를 반환합니다
		 *       (SO_REUSEADDR 이 유사하지만 의미가 달라 대체하지 않습니다).
		 */
		[[nodiscard]] IoResult<void_t> SetReusePort(bool_t _isEnabled);

		/** @brief SO_KEEPALIVE — 유휴 연결에 keepalive 프로브를 보내 죽은 피어를 감지합니다. */
		[[nodiscard]] IoResult<void_t> SetKeepAlive(bool_t _isEnabled);

		/**
		 * @brief keepalive 타이밍을 조정합니다(SetKeepAlive(true) 와 함께 써야 의미가 있습니다).
		 * @param _idle 유휴 상태가 이만큼 지속되면 첫 프로브를 보냅니다.
		 * @param _interval 프로브 간 간격.
		 * @note Windows 는 프로브 **횟수**를 설정할 수 없어(SIO_KEEPALIVE_VALS 가 idle/interval 만 받음)
		 *       _count 를 두지 않았습니다 — 플랫폼별로 다른 값을 노출하지 않기 위한 선택입니다.
		 */
		[[nodiscard]] IoResult<void_t> SetKeepAliveTiming(std::chrono::seconds _idle, std::chrono::seconds _interval);

		/** @brief SO_LINGER — close() 시 미전송 데이터를 얼마나 기다릴지 정합니다. */
		[[nodiscard]] IoResult<void_t> SetLinger(LingerOption _option);

		/** @brief SO_RCVBUF — 커널 수신 버퍼 크기(바이트). 커널이 요청값을 조정할 수 있습니다. */
		[[nodiscard]] IoResult<void_t> SetReceiveBufferSize(int_t _bytes);
		/** @brief SO_SNDBUF — 커널 송신 버퍼 크기(바이트). 커널이 요청값을 조정할 수 있습니다. */
		[[nodiscard]] IoResult<void_t> SetSendBufferSize(int_t _bytes);

		/** @brief SO_BROADCAST — 데이터그램 소켓이 브로드캐스트 주소로 송신할 수 있게 합니다. */
		[[nodiscard]] IoResult<void_t> SetBroadcast(bool_t _isEnabled);

		/**
		 * @brief IPV6_V6ONLY — false 면 IPv6 소켓이 IPv4-mapped 주소로 IPv4 연결도 받습니다.
		 * @note AF_INET6 소켓에만 유효합니다. 이 값의 기본값은 플랫폼마다 달라(Windows=true,
		 *       대부분의 Linux=true) 듀얼스택을 원하면 명시적으로 false 를 설정해야 합니다.
		 */
		[[nodiscard]] IoResult<void_t> SetIpV6Only(bool_t _isEnabled);

		/**
		 * @brief 이름 있는 setter 가 없는 옵션을 직접 설정하는 탈출구입니다.
		 *
		 * 라이브러리가 모든 소켓 옵션을 알 수는 없으므로, 플랫폼 고유/드문 옵션을 위해 원시 경로를
		 * 열어 둡니다(예: IP_TOS, TCP_QUICKACK).
		 *
		 * @param _level setsockopt 의 level(SOL_SOCKET, IPPROTO_TCP 등).
		 * @param _name  옵션 이름.
		 * @param _value 옵션 값의 바이트 표현.
		 */
		[[nodiscard]] IoResult<void_t> SetRawOption(int_t _level, int_t _name, std::span<const ne::byte_t> _value);

	public:
		/**
		 * @brief 이 소켓에 바인딩된 로컬 포트를 반환합니다(조회 실패 시 0).
		 *
		 * 포트 0 으로 Bind 해 커널이 임시 포트를 배정한 뒤 그 값을 알아내는 것이 주 용도입니다.
		 */
		[[nodiscard]] uint16_t LocalPort() const noexcept;

		/** @brief 로컬 주소(IP 문자열 + 포트). 조회 실패 시 에러. */
		[[nodiscard]] IoResult<SocketAddress> LocalAddress() const;

		/**
		 * @brief 상대 주소(IP 문자열 + 포트). 연결되지 않은 소켓이면 에러.
		 *
		 * 액세스 로그·레이트 리밋·차단 목록처럼 "누가 접속했는가" 를 알아야 하는 기능의 전제입니다.
		 */
		[[nodiscard]] IoResult<SocketAddress> PeerAddress() const;

	public:
		/** @brief 연결의 한쪽 또는 양쪽 방향을 닫습니다(소켓 핸들은 유지 — 닫으려면 Close()). */
		[[nodiscard]] IoResult<void_t> Shutdown(ShutdownMode _mode = ShutdownMode::SEND);
		[[nodiscard]] IoResult<void_t> Close();


	public:
		[[nodiscard]] socket_t Handle() const noexcept { return handle.Get(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
		[[nodiscard]] bool_t IsRegisteredIo() const noexcept { return isRegisteredIo; }
	};
}
