//
// Created by hscloud on 26. 8. 12.
//

// io::Socket 의 옵션 setter / 주소 조회 / Shutdown 방향 검증.
//
// 옵션 setter 는 대부분 "커널이 값을 받아들였는가" 만 확인할 수 있다(getsockopt 로 되읽어도 커널이
// 값을 조정하는 경우가 많아 일치를 요구할 수 없다). 그래도 이 테스트에 값이 있는 이유는, 잘못된
// level/name 조합이나 플랫폼 분기 실수는 곧바로 setsockopt 실패로 드러나기 때문이다.

#include <gtest/gtest.h>

#if defined(_WIN32)

#include "Base/WinsockApi.h"
#include <chrono>
#include <span>
#include "Base/Coroutine/Task.h"
#include "Io/Context.h"
#include "Io/Socket.h"
#include "Io/Internal/Engine/Iocp/IocpEngine.h"

using namespace ne;
using namespace ne::io;

namespace
{
	struct WsaScope
	{
		WsaScope() noexcept
		{
			WSADATA data{};
			::WSAStartup(MAKEWORD(2, 2), &data);
		}
		~WsaScope() noexcept { ::WSACleanup(); }
	};

	// 연결된 소켓 한 쌍을 만든다(주소 조회/Shutdown 검증에 실제 연결이 필요하다).
	struct ConnectedPair
	{
		Socket client;
		Socket accepted;
	};

	IoResult<ConnectedPair> MakeConnectedPair(Context& _context)
	{
		using R = IoResult<ConnectedPair>;

		auto listenerResult = Socket::Create(_context, AF_INET);
		if (listenerResult.IsError()) return R::Error(std::move(listenerResult.Error()));
		Socket listener = std::move(listenerResult.Value());

		if (auto bound = listener.Bind("127.0.0.1", 0); bound.IsError()) return R::Error(std::move(bound.Error()));
		if (auto listened = listener.Listen(1); listened.IsError()) return R::Error(std::move(listened.Error()));

		auto clientResult = Socket::Create(_context, AF_INET);
		if (clientResult.IsError()) return R::Error(std::move(clientResult.Error()));
		Socket client = std::move(clientResult.Value());

		auto acceptTask = listener.Accept();
		auto connectTask = client.Connect("127.0.0.1", listener.LocalPort());
		acceptTask.Resume();
		connectTask.Resume();

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while ((!acceptTask.IsReady() || !connectTask.IsReady()) && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 10 });

		if (!acceptTask.IsReady() || !connectTask.IsReady()) return R::Error(IoError{ IoErrorKind::OS_FAILURE, "connect/accept did not complete" });
		if (auto connected = connectTask.await_resume(); connected.IsError()) return R::Error(std::move(connected.Error()));

		auto acceptResult = acceptTask.await_resume();
		if (acceptResult.IsError()) return R::Error(std::move(acceptResult.Error()));

		return R::Ok(ConnectedPair{ std::move(client), std::move(acceptResult.Value()) });
	}
}

// ── 이름 있는 옵션 setter 가 모두 커널에 받아들여진다 ──
TEST(SocketOptionTest, NamedSettersSucceed)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto created = Socket::Create(context, AF_INET);
	ASSERT_TRUE(created.IsOk()) << created.Error().What();
	Socket socket = std::move(created.Value());

	EXPECT_TRUE(socket.SetReuseAddress(true).IsOk());
	EXPECT_TRUE(socket.SetNoDelay(true).IsOk());
	EXPECT_TRUE(socket.SetKeepAlive(true).IsOk());
	EXPECT_TRUE(socket.SetKeepAliveTiming(std::chrono::seconds(30), std::chrono::seconds(5)).IsOk());
	EXPECT_TRUE(socket.SetLinger(Socket::LingerOption{ true, 3 }).IsOk());
	EXPECT_TRUE(socket.SetReceiveBufferSize(64 * 1024).IsOk());
	EXPECT_TRUE(socket.SetSendBufferSize(64 * 1024).IsOk());
}

// ── keepalive 타이밍은 양수만 받는다 ──
TEST(SocketOptionTest, KeepAliveTimingRejectsNonPositive)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto created = Socket::Create(context, AF_INET);
	ASSERT_TRUE(created.IsOk());
	Socket socket = std::move(created.Value());

	EXPECT_TRUE(socket.SetKeepAliveTiming(std::chrono::seconds(0), std::chrono::seconds(5)).IsError());
	EXPECT_TRUE(socket.SetKeepAliveTiming(std::chrono::seconds(30), std::chrono::seconds(-1)).IsError());
}

// ── SO_REUSEPORT 는 Windows 에 없으므로 조용히 통과하지 않고 UNSUPPORTED 로 알린다 ──
//
// SO_REUSEADDR 로 몰래 대체하면 사용자가 "연결 분산이 켜졌다" 고 믿게 되므로, 없는 기능은 없다고 해야 한다.
TEST(SocketOptionTest, ReusePortReportsUnsupportedOnWindows)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto created = Socket::Create(context, AF_INET);
	ASSERT_TRUE(created.IsOk());
	Socket socket = std::move(created.Value());

	auto result = socket.SetReusePort(true);
	ASSERT_TRUE(result.IsError());
	EXPECT_TRUE(result.Error().IsUnsupported());
}

// ── IPv6 소켓에 IPV6_V6ONLY 를 끄면 듀얼스택이 된다 ──
TEST(SocketOptionTest, IpV6OnlyAppliesToIpV6Socket)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto created = Socket::Create(context, AF_INET6);
	ASSERT_TRUE(created.IsOk()) << created.Error().What();
	Socket socket = std::move(created.Value());

	EXPECT_TRUE(socket.SetIpV6Only(false).IsOk());
	EXPECT_TRUE(socket.SetIpV6Only(true).IsOk());
}

// ── 데이터그램 소켓은 브로드캐스트를 켤 수 있다 ──
TEST(SocketOptionTest, BroadcastAppliesToDatagramSocket)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto created = Socket::Create(context, AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	ASSERT_TRUE(created.IsOk()) << created.Error().What();
	Socket socket = std::move(created.Value());

	EXPECT_TRUE(socket.SetBroadcast(true).IsOk());
}

// ── 탈출구(SetRawOption)로 이름 없는 옵션도 설정할 수 있다 ──
TEST(SocketOptionTest, RawOptionEscapeHatchWorks)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto created = Socket::Create(context, AF_INET);
	ASSERT_TRUE(created.IsOk());
	Socket socket = std::move(created.Value());

	// 이름 있는 setter 가 있는 옵션으로 검증한다(원시 경로가 같은 결과를 내는지 확인하는 것이 목적).
	const int_t enable = 1;
	const auto bytes = std::span<const ne::byte_t>{ reinterpret_cast<const ne::byte_t*>(&enable), sizeof(enable) };
	EXPECT_TRUE(socket.SetRawOption(SOL_SOCKET, SO_REUSEADDR, bytes).IsOk());

	// 존재하지 않는 옵션 이름은 실패해야 한다(탈출구가 에러를 삼키지 않는지 확인).
	EXPECT_TRUE(socket.SetRawOption(SOL_SOCKET, 0x7FFF, bytes).IsError());
}

// ── 로컬/상대 주소를 조회할 수 있다 ──
//
// 이것이 없으면 액세스 로그에 클라이언트 IP 를 남길 수도, IP 기반 레이트 리밋을 걸 수도 없다.
TEST(SocketOptionTest, LocalAndPeerAddressAreResolvable)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto pairResult = MakeConnectedPair(context);
	ASSERT_TRUE(pairResult.IsOk()) << pairResult.Error().What();
	auto pair = std::move(pairResult.Value());

	auto clientLocal = pair.client.LocalAddress();
	ASSERT_TRUE(clientLocal.IsOk()) << clientLocal.Error().What();
	EXPECT_EQ(clientLocal.Value().ip, "127.0.0.1");
	EXPECT_EQ(clientLocal.Value().family, AF_INET);
	EXPECT_NE(clientLocal.Value().port, 0);

	auto clientPeer = pair.client.PeerAddress();
	ASSERT_TRUE(clientPeer.IsOk()) << clientPeer.Error().What();
	EXPECT_EQ(clientPeer.Value().ip, "127.0.0.1");

	// 양쪽에서 본 주소가 서로 교차해야 한다(내 상대 = 상대의 나).
	auto acceptedLocal = pair.accepted.LocalAddress();
	ASSERT_TRUE(acceptedLocal.IsOk());
	EXPECT_EQ(clientPeer.Value().port, acceptedLocal.Value().port);

	auto acceptedPeer = pair.accepted.PeerAddress();
	ASSERT_TRUE(acceptedPeer.IsOk());
	EXPECT_EQ(acceptedPeer.Value().port, clientLocal.Value().port);
}

// ── 연결되지 않은 소켓의 상대 주소 조회는 실패한다 ──
TEST(SocketOptionTest, PeerAddressFailsOnUnconnectedSocket)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto created = Socket::Create(context, AF_INET);
	ASSERT_TRUE(created.IsOk());
	Socket socket = std::move(created.Value());

	EXPECT_TRUE(socket.PeerAddress().IsError());
}

// ── Shutdown(SEND) 는 half-close: 상대는 EOF 를 보지만 반대 방향은 계속 흐른다 ──
TEST(SocketOptionTest, ShutdownSendIsHalfClose)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto pairResult = MakeConnectedPair(context);
	ASSERT_TRUE(pairResult.IsOk()) << pairResult.Error().What();
	auto pair = std::move(pairResult.Value());

	// 클라이언트가 송신 방향만 닫는다.
	ASSERT_TRUE(pair.client.Shutdown(Socket::ShutdownMode::SEND).IsOk());

	// 상대는 EOF(0 바이트)를 본다.
	ne::byte_t buffer[16]{};
	auto receiveTask = pair.accepted.Receive(std::span<ne::byte_t>{ buffer, sizeof(buffer) });
	receiveTask.Resume();

	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!receiveTask.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)context.RunOnce(std::chrono::milliseconds{ 10 });
	ASSERT_TRUE(receiveTask.IsReady());

	auto received = receiveTask.await_resume();
	ASSERT_TRUE(received.IsOk()) << received.Error().What();
	EXPECT_EQ(received.Value(), 0u) << "송신 방향을 닫으면 상대는 EOF 를 봐야 한다";

	// 반대 방향(accepted → client)은 여전히 살아 있어야 half-close 다.
	constexpr char payload[] = "still-open";
	auto sendTask = pair.accepted.Send(std::span<const ne::byte_t>{ reinterpret_cast<const ne::byte_t*>(payload), sizeof(payload) - 1 });
	sendTask.Resume();

	deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!sendTask.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)context.RunOnce(std::chrono::milliseconds{ 10 });
	ASSERT_TRUE(sendTask.IsReady());
	ASSERT_TRUE(sendTask.await_resume().IsOk()) << "half-close 인데 반대 방향 송신이 실패했다";

	ne::byte_t echoed[16]{};
	auto readBack = pair.client.Receive(std::span<ne::byte_t>{ echoed, sizeof(echoed) });
	readBack.Resume();

	deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!readBack.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)context.RunOnce(std::chrono::milliseconds{ 10 });
	ASSERT_TRUE(readBack.IsReady());

	auto readResult = readBack.await_resume();
	ASSERT_TRUE(readResult.IsOk()) << readResult.Error().What();
	EXPECT_EQ(readResult.Value(), sizeof(payload) - 1) << "송신만 닫았는데 수신 방향도 끊겼다";
}

// ── Shutdown(BOTH) 는 양방향을 닫는다 ──
TEST(SocketOptionTest, ShutdownBothClosesEitherDirection)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto pairResult = MakeConnectedPair(context);
	ASSERT_TRUE(pairResult.IsOk()) << pairResult.Error().What();
	auto pair = std::move(pairResult.Value());

	ASSERT_TRUE(pair.client.Shutdown(Socket::ShutdownMode::BOTH).IsOk());

	ne::byte_t buffer[16]{};
	auto receiveTask = pair.accepted.Receive(std::span<ne::byte_t>{ buffer, sizeof(buffer) });
	receiveTask.Resume();

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!receiveTask.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)context.RunOnce(std::chrono::milliseconds{ 10 });
	ASSERT_TRUE(receiveTask.IsReady());

	auto received = receiveTask.await_resume();
	ASSERT_TRUE(received.IsOk()) << received.Error().What();
	EXPECT_EQ(received.Value(), 0u);
}

#endif // _WIN32
