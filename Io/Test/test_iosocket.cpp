#include <gtest/gtest.h>

#if defined(_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <chrono>
#include <cstring>
#include <span>
#include "Io/Context/Context.h"
#include "Io/Socket/Socket.h"
#include "Base/Coroutine/Task.h"
#include "Io/Engine/Iocp/IocpEngine.h"

using namespace ne;
using namespace ne::io;

namespace
{
	bool_t MakeConnectedPair(SOCKET& _a, SOCKET& _b)
	{
		const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listener == INVALID_SOCKET) return false;

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
		address.sin_port = 0;

		int_t length = static_cast<int_t>(sizeof(address));
		if (::bind(listener, reinterpret_cast<sockaddr*>(&address), length) != 0 ||
			::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0 ||
			::listen(listener, 1) != 0)
		{
			::closesocket(listener);
			return false;
		}

		_a = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (_a == INVALID_SOCKET || ::connect(_a, reinterpret_cast<sockaddr*>(&address), static_cast<int_t>(sizeof(address))) != 0)
		{
			::closesocket(listener);
			if (_a != INVALID_SOCKET) ::closesocket(_a);
			return false;
		}

		_b = ::accept(listener, nullptr, nullptr);
		::closesocket(listener);
		return _b != INVALID_SOCKET;
	}

	struct WsaScope
	{
		WsaScope() noexcept { WSADATA data; ::WSAStartup(MAKEWORD(2, 2), &data); }
		~WsaScope() noexcept { ::WSACleanup(); }
	};

	template <typename T>
	T Drive(Context& _context, ne::Task<T>& _task)
	{
		_task.Resume();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!_task.IsReady() && std::chrono::steady_clock::now() < deadline)
			(void_t)_context.RunOnce(std::chrono::milliseconds{ 50 });
		return _task.await_resume();
	}
}

// ── Socket: 비동기 Send → Receive 왕복 (Level 3) ──
TEST(SocketLevel3Test, SendThenReceive)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	SOCKET rawA = INVALID_SOCKET;
	SOCKET rawB = INVALID_SOCKET;
	ASSERT_TRUE(MakeConnectedPair(rawA, rawB));

	auto adoptedA = Socket::Attach(static_cast<socket_t>(rawA), context);
	auto adoptedB = Socket::Attach(static_cast<socket_t>(rawB), context);
	ASSERT_TRUE(adoptedA.IsOk());
	ASSERT_TRUE(adoptedB.IsOk());
	Socket sender = std::move(adoptedA.Value());
	Socket receiver = std::move(adoptedB.Value());

	const char payload[] = "level3-async-socket";
	const std::size_t length = sizeof(payload) - 1;

	auto sendTask = sender.Send(std::span<const ne::byte_t>{ reinterpret_cast<const ne::byte_t*>(payload), length });
	auto sendResult = Drive(context, sendTask);
	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();
	EXPECT_EQ(sendResult.Value(), length);

	ne::byte_t buffer[32]{};
	auto receiveTask = receiver.Receive(std::span<ne::byte_t>{ buffer, length });
	auto receiveResult = Drive(context, receiveTask);
	ASSERT_TRUE(receiveResult.IsOk()) << receiveResult.Error().What();
	EXPECT_EQ(receiveResult.Value(), length);
	EXPECT_EQ(std::memcmp(buffer, payload, length), 0);

	// sender/receiver 소멸자가 소켓을 닫는다(Adopt 로 소유권 이전).
}

// ── Socket: scatter/gather SendV → ReceiveV 왕복 (WSASend/WSARecv 멀티 WSABUF) ──
TEST(SocketLevel3Test, SendVThenReceiveVRoundTrip)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	SOCKET rawA = INVALID_SOCKET;
	SOCKET rawB = INVALID_SOCKET;
	ASSERT_TRUE(MakeConnectedPair(rawA, rawB));

	auto adoptedA = Socket::Attach(static_cast<socket_t>(rawA), context);
	auto adoptedB = Socket::Attach(static_cast<socket_t>(rawB), context);
	ASSERT_TRUE(adoptedA.IsOk());
	ASSERT_TRUE(adoptedB.IsOk());
	Socket sender = std::move(adoptedA.Value());
	Socket receiver = std::move(adoptedB.Value());

	char partA[] = "scatter-";
	char partB[] = "gather-";
	char partC[] = "socket";
	BufferChain sendChain;
	sendChain.Append(BufferView{ reinterpret_cast<ne::byte_t*>(partA), sizeof(partA) - 1 });
	sendChain.Append(BufferView{ reinterpret_cast<ne::byte_t*>(partB), sizeof(partB) - 1 });
	sendChain.Append(BufferView{ reinterpret_cast<ne::byte_t*>(partC), sizeof(partC) - 1 });
	const std::size_t totalLength = sendChain.TotalSize();

	auto sendTask = sender.Sendv(sendChain);
	auto sendResult = Drive(context, sendTask);
	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();
	EXPECT_EQ(sendResult.Value(), totalLength);

	ne::byte_t bufferA[8]{};
	ne::byte_t bufferB[7]{};
	ne::byte_t bufferC[6]{};
	BufferChain receiveChain;
	receiveChain.Append(BufferView{ bufferA, sizeof(bufferA) });
	receiveChain.Append(BufferView{ bufferB, sizeof(bufferB) });
	receiveChain.Append(BufferView{ bufferC, sizeof(bufferC) });

	auto receiveTask = receiver.Receivev(receiveChain);
	auto receiveResult = Drive(context, receiveTask);
	ASSERT_TRUE(receiveResult.IsOk()) << receiveResult.Error().What();
	EXPECT_EQ(receiveResult.Value(), totalLength);
	EXPECT_EQ(std::memcmp(bufferA, partA, sizeof(bufferA)), 0);
	EXPECT_EQ(std::memcmp(bufferB, partB, sizeof(bufferB)), 0);
	EXPECT_EQ(std::memcmp(bufferC, partC, sizeof(bufferC)), 0);
}

TEST(SocketLevel3Test, AdoptInvalidFails)
{
	IocpEngine engine;
	Context context{ engine };
	auto adopted = Socket::Attach(InvalidSocket, context);
	EXPECT_TRUE(adopted.IsError());
}

// ── Accept(AcceptEx) + Connect(ConnectEx) 왕복 → 연결된 소켓으로 데이터 송수신 ──
TEST(SocketLevel3Test, AcceptConnectRoundTrip)
{
	const WsaScope wsa;
	IocpEngine engine;
	Context context{ engine };

	// 저수준 listen 소켓 준비 후 Adopt.
	const SOCKET rawListener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	ASSERT_NE(rawListener, INVALID_SOCKET);
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
	address.sin_port = 0;
	int_t addressLength = static_cast<int_t>(sizeof(address));
	ASSERT_EQ(::bind(rawListener, reinterpret_cast<sockaddr*>(&address), addressLength), 0);
	ASSERT_EQ(::getsockname(rawListener, reinterpret_cast<sockaddr*>(&address), &addressLength), 0);
	ASSERT_EQ(::listen(rawListener, 1), 0);
	const uint16_t port = ::ntohs(address.sin_port);

	auto listenAdopt = Socket::Attach(static_cast<socket_t>(rawListener), context);
	ASSERT_TRUE(listenAdopt.IsOk());
	Socket listenSocket = std::move(listenAdopt.Value());

	// Accept 와 Connect 는 서로 의존 — 둘 다 제출한 뒤 함께 구동한다.
	auto acceptTask = listenSocket.Accept();
	auto connectTask = Socket::Connect(context, "127.0.0.1", port);
	acceptTask.Resume();
	connectTask.Resume();

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while ((!acceptTask.IsReady() || !connectTask.IsReady()) && std::chrono::steady_clock::now() < deadline)
		(void_t)context.RunOnce(std::chrono::milliseconds{ 50 });

	ASSERT_TRUE(acceptTask.IsReady());
	ASSERT_TRUE(connectTask.IsReady());

	auto acceptResult = acceptTask.await_resume();
	auto connectResult = connectTask.await_resume();
	ASSERT_TRUE(acceptResult.IsOk()) << acceptResult.Error().What();
	ASSERT_TRUE(connectResult.IsOk()) << connectResult.Error().What();

	Socket server = std::move(acceptResult.Value());
	Socket client = std::move(connectResult.Value());

	// 연결된 소켓으로 데이터 왕복 검증.
	const char payload[] = "accept-connect-roundtrip";
	const std::size_t length = sizeof(payload) - 1;

	auto sendTask = client.Send(std::span<const ne::byte_t>{ reinterpret_cast<const ne::byte_t*>(payload), length });
	auto sendResult = Drive(context, sendTask);
	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();
	EXPECT_EQ(sendResult.Value(), length);

	ne::byte_t buffer[32]{};
	auto receiveTask = server.Receive(std::span<ne::byte_t>{ buffer, length });
	auto receiveResult = Drive(context, receiveTask);
	ASSERT_TRUE(receiveResult.IsOk()) << receiveResult.Error().What();
	EXPECT_EQ(receiveResult.Value(), length);
	EXPECT_EQ(std::memcmp(buffer, payload, length), 0);
}

// ── UDP(SOCK_DGRAM) SendTo → ReceiveFrom 왕복 — 발신자 주소까지 검증 ──
TEST(SocketLevel3Test, SendToReceiveFromRoundTrip)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	// 두 UDP 소켓을 loopback 에 바인딩(포트는 OS 가 골라줌) — connect 없이 SendTo/ReceiveFrom 만 사용.
	const SOCKET rawA = ::WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	const SOCKET rawB = ::WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	ASSERT_NE(rawA, INVALID_SOCKET);
	ASSERT_NE(rawB, INVALID_SOCKET);

	sockaddr_in addressA{};
	addressA.sin_family = AF_INET;
	addressA.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
	int_t addressALength = static_cast<int_t>(sizeof(addressA));
	ASSERT_EQ(::bind(rawA, reinterpret_cast<sockaddr*>(&addressA), addressALength), 0);
	ASSERT_EQ(::getsockname(rawA, reinterpret_cast<sockaddr*>(&addressA), &addressALength), 0);
	const uint16_t portA = ::ntohs(addressA.sin_port);

	sockaddr_in addressB{};
	addressB.sin_family = AF_INET;
	addressB.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
	int_t addressBLength = static_cast<int_t>(sizeof(addressB));
	ASSERT_EQ(::bind(rawB, reinterpret_cast<sockaddr*>(&addressB), addressBLength), 0);
	ASSERT_EQ(::getsockname(rawB, reinterpret_cast<sockaddr*>(&addressB), &addressBLength), 0);
	const uint16_t portB = ::ntohs(addressB.sin_port);

	auto adoptedA = Socket::Attach(static_cast<socket_t>(rawA), context);
	auto adoptedB = Socket::Attach(static_cast<socket_t>(rawB), context);
	ASSERT_TRUE(adoptedA.IsOk());
	ASSERT_TRUE(adoptedB.IsOk());
	Socket socketA = std::move(adoptedA.Value());
	Socket socketB = std::move(adoptedB.Value());

	const char payload[] = "udp-sendto-receivefrom";
	const std::size_t length = sizeof(payload) - 1;

	auto sendTask = socketA.SendTo(std::span<const ne::byte_t>{ reinterpret_cast<const ne::byte_t*>(payload), length },
	                                "127.0.0.1", portB);
	auto sendResult = Drive(context, sendTask);
	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();
	EXPECT_EQ(sendResult.Value(), length);

	ne::byte_t buffer[64]{};
	ne::string_t ip;
	ne::uint16_t port;
	auto receiveTask = socketB.ReceiveFrom(std::span<ne::byte_t>{ buffer, sizeof(buffer) }, ip, port);
	auto receiveResult = Drive(context, receiveTask);
	ASSERT_TRUE(receiveResult.IsOk()) << receiveResult.Error().What();
	EXPECT_EQ(receiveResult.Value(), length);
	EXPECT_EQ(std::memcmp(buffer, payload, length), 0);
	EXPECT_EQ(ip, "127.0.0.1");
	EXPECT_EQ(port, portA);
}

#endif // _WIN32
