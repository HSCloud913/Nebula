//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <chrono>
#include <vector>
#include "NetworkType.h"
#include "Handle.h"
#include "Result.h"
#include "Error.h"
#include "Coroutine/Task.h"
#include "Engine/IIoEngine.h"

BEGIN_NS(ne::network)
	// 소켓의 주소 체계. fd 생성 시점에 고정되며, 이후 Bind/Connect 는 이 체계에
	// 맞는 주소만 받아들인다 (BSD 소켓 API 자체가 socket() 호출 시 패밀리를 고정하기 때문).
	enum class AddressFamily : uint8_t
	{
		IPv4,
		IPv6,
	};

	// 소켓 생성 옵션 (비트플래그). 기본 None 은 현행 동작(::socket).
	enum class SocketCreateFlags : uint32_t
	{
		None         = 0,
		// Windows: WSA_FLAG_REGISTERED_IO 로 생성해 RIO(RIORegisterBuffer/RIOCreateRequestQueue)를 쓸 수 있게 한다.
		// POSIX: no-op — io_uring registered buffer 는 소켓 생성 플래그가 필요 없다.
		RegisteredIo = 1u << 0,
	};

	[[nodiscard]] constexpr SocketCreateFlags operator|(const SocketCreateFlags _a, const SocketCreateFlags _b) noexcept
	{
		return static_cast<SocketCreateFlags>(static_cast<uint32_t>(_a) | static_cast<uint32_t>(_b));
	}

	[[nodiscard]] constexpr bool_t HasFlag(const SocketCreateFlags _set, const SocketCreateFlags _bit) noexcept
	{
		return (static_cast<uint32_t>(_set) & static_cast<uint32_t>(_bit)) == static_cast<uint32_t>(_bit);
	}

	// 소켓 RAII 래퍼.
	// 역할: fd 생성/소멸, Connect/Bind/Listen/Accept, 소켓 옵션.
	// 송수신 없음 — 송수신은 상위 Stream 레이어 책임.
	class Socket
	{
	private:
		Socket(socket_t _fd, AddressFamily _family, int_t _type, int_t _protocol);

	public:
		~Socket() = default;

		NEBULA_NON_COPYABLE(Socket)
		NEBULA_DEFAULT_MOVE(Socket)

	private:
#if defined(_WIN32)
		using SocketHandle = ne::Handle<
			socket_t,
			decltype([](const socket_t _socket) { ::closesocket(_socket); }),
			INVALID_SOCKET
		>;
#elif defined(IS_POSIX)
		using SocketHandle = ne::Handle<
			socket_t,
			decltype([](const socket_t _socket) { ::close(_socket); }),
			static_cast<socket_t>(-1)
		>;
#endif

		SocketHandle handle;
		AddressFamily family{ AddressFamily::IPv4 };
		int_t type{};
		int_t protocol{};

	public:
		[[nodiscard]] static Result<Socket, OsError> Create(AddressFamily _family, int_t _type, int_t _protocol,
			SocketCreateFlags _flags = SocketCreateFlags::None);

		// _address 가 IPv4/IPv6 리터럴이면 파싱만으로, 호스트명이면 DNS 조회로 패밀리를 추정.
		// Create 에 넘길 패밀리를 미리 정하는 용도 — 실제 주소 해석은 Bind/Connect 가
		// 소켓 자신의 패밀리를 기준으로 다시 수행한다. DNS 조회는 전용 워커 스레드로
		// 오프로드해 호출 스레드(이벤트 루프)를 막지 않는다.
		[[nodiscard]] static ne::Task<ne::Result<AddressFamily, ne::OsError>> ResolveFamily(string_view_t _address);

	public: /* Option */
		[[nodiscard]] ne::Result<void, ne::OsError> SetReuseAddress(bool_t _enable);
		[[nodiscard]] ne::Result<void, ne::OsError> SetNoDelay(bool_t _enable);
		[[nodiscard]] ne::Result<void, ne::OsError> SetNonBlocking(bool_t _enable);
		[[nodiscard]] ne::Result<void, ne::OsError> SetSendTimeout(std::chrono::milliseconds _timeout);
		[[nodiscard]] ne::Result<void, ne::OsError> SetReceiveTimeout(std::chrono::milliseconds _timeout);

	public: /* Client */
		// 완전 blocking connect — DNS 조회는 비동기이지만 실제 ::connect() syscall은 소켓이
		// blocking 상태인 채로 DNS 워커 스레드 위에서 실행되어, 상대가 응답 없으면 그 스레드를
		// OS 기본 connect 타임아웃(수십 초)까지 점유한다. libssh2 같은 의도적으로 완전
		// blocking 인 세션과 짝지어 쓸 때만 사용할 것 — 그 외에는 아래 엔진 오버로드를 쓴다.
		[[nodiscard]] ne::Task<ne::Result<void, ne::OsError>> Connect(string_view_t _address, uint16_t _port);

		// non-blocking connect — 후보별로 SetNonBlocking(true) 후 connect() 를 시도하고,
		// EINPROGRESS/WSAEWOULDBLOCK 이면 엔진의 Watch(Write|Error) 로 완료를 기다린 뒤
		// SO_ERROR 로 성공/실패를 확정한다. DNS 워커 스레드를 connect 완료까지 붙잡지 않는다.
		[[nodiscard]] ne::Task<ne::Result<void, ne::OsError>> Connect(string_view_t _address, uint16_t _port, ne::io::IIoEngine& _engine);

	public: /* Server */
		[[nodiscard]] ne::Task<ne::Result<void, ne::OsError>> Bind(string_view_t _address, uint16_t _port);
		[[nodiscard]] ne::Result<void, ne::OsError> Listen(int_t _backlog = SOMAXCONN);
		[[nodiscard]] ne::Result<Socket, ne::OsError> Accept();

	private:
		// ResolveFamily 의 blocking 구현 — DNS 워커 스레드에서 실행된다.
		[[nodiscard]] static ne::Result<AddressFamily, ne::OsError> ResolveFamilyBlocking(string_view_t _address);

		// 소켓 자신의 family 를 기준으로 주소를 해석한다 (다른 패밀리의 리터럴/호스트명은 에러).
		// Bind 용 — 호스트명이 여러 주소로 풀려도 첫 번째만 사용한다.
		[[nodiscard]] ne::Result<sockaddr_storage, ne::OsError> ResolveAddress(string_view_t _address, uint16_t _port) const;

		// Connect 용 — 호스트명이 여러 주소(A/AAAA 레코드)로 풀리면 전부 반환해 페일오버를 지원한다.
		[[nodiscard]] ne::Result<std::vector<sockaddr_storage>, ne::OsError> ResolveCandidates(string_view_t _address, uint16_t _port) const;

		// candidates 를 순서대로 connect() 시도(완전 blocking). 이전 시도가 실패하면 소켓을 새로
		// 열어 재시도한다(실패한 소켓으로 connect() 를 재시도하는 것은 플랫폼별로 결과가 불명확하기 때문).
		[[nodiscard]] ne::Result<void, ne::OsError> ConnectResolved(const std::vector<sockaddr_storage>& _candidates);

		// candidates 를 순서대로 non-blocking connect 시도 — 각 후보에 대해 SetNonBlocking(true) 후
		// connect() 를 걸고, EINPROGRESS/WSAEWOULDBLOCK 이면 _engine 으로 완료를 기다린 뒤
		// SO_ERROR 로 성공/실패를 확정한다. 실패하면 다음 후보로(소켓 재생성) 넘어간다.
		[[nodiscard]] ne::Task<ne::Result<void, ne::OsError>> ConnectResolvedAsync(const std::vector<sockaddr_storage>& _candidates, ne::io::IIoEngine& _engine);

	public:
		[[nodiscard]] socket_t Handle() const noexcept { return handle.Get(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
		[[nodiscard]] AddressFamily Family() const noexcept { return family; }
	};

END_NS
