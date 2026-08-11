//
// Created by hscloud on 26. 8. 11.
//

// io::Socket 을 **리액터 엔진**과 조합해 검증한다.
//
// 기존 소켓 테스트는 전부 IocpEngine(프로액터)만 인스턴스화했고, WsaPollEngine 테스트는 손으로 만든
// 원시 소켓에 직접 FIONBIO 를 걸어 썼다. 그래서 "io::Socket 이 만든 소켓 + 리액터 엔진" 이라는 실제
// 조합이 한 번도 실행되지 않았고, Socket 이 소켓을 논블로킹으로 만들지 않아 리액터의 ::recv/::accept/
// ::connect 가 이벤트 루프 스레드를 블로킹한다는 사실이 드러나지 않았다.
//
// 아래 테스트는 모두 **단일 스레드**에서 Context 를 직접 돌린다. 어느 한 시스템 콜이라도 블로킹되면
// 루프가 진전하지 못해 데드라인에 걸리므로, 회귀는 hang 이 아니라 명시적 실패로 나타난다.

#include <gtest/gtest.h>

#if defined(_WIN32)

#include "Base/WinsockApi.h"
#include <chrono>
#include <cstring>
#include <span>
#include "Base/Coroutine/Task.h"
#include "Io/Context.h"
#include "Io/Socket.h"
#include "Io/Internal/Engine/WsaPoll/WsaPollEngine.h"

using namespace ne;
using namespace ne::io;

namespace
{
	// WsaPollEngine 은 생성자에서 wake 용 소켓 쌍을 만들므로 Winsock 초기화가 선행되어야 한다
	// (Io/Test 에는 Network/Test 의 env_winsock.cpp 같은 전역 환경이 없어 테스트마다 세운다).
	struct WsaScope
	{
		WsaScope() noexcept
		{
			WSADATA data{};
			::WSAStartup(MAKEWORD(2, 2), &data);
		}
		~WsaScope() noexcept { ::WSACleanup(); }
	};

	template <typename T>
	bool_t Drive(Context& _context, ne::Task<T>& _task, const std::chrono::seconds _timeout = std::chrono::seconds(5))
	{
		_task.Resume();
		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while (!_task.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 10 });

		return _task.IsReady();
	}
}

// ── Socket::Create 로 만든 소켓이 리액터에서 Connect→Send→Receive 를 완주한다 ──
//
// 블로킹 소켓이면 클라이언트의 ::connect 가 핸드셰이크 내내 루프를 잡고, 서버의 ::accept 는 접속 전에
// 블로킹되어 단일 스레드에서 영구 교착된다.
TEST(SocketReactorTest, ConnectAcceptSendReceiveRoundTrip)
{
	const WsaScope wsa;
	WsaPollEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto listenerResult = Socket::Create(context, AF_INET, SOCK_STREAM, IPPROTO_TCP);
	ASSERT_TRUE(listenerResult.IsOk()) << listenerResult.Error().What();
	Socket listener = std::move(listenerResult.Value());

	ASSERT_TRUE(listener.Bind("127.0.0.1", 0).IsOk());
	ASSERT_TRUE(listener.Listen(1).IsOk());

	// 커널이 배정한 임시 포트를 조회한다(io::Socket 에 주소 접근자가 없어 원시 API 를 쓴다).
	sockaddr_in bound{};
	int_t boundLength = static_cast<int_t>(sizeof(bound));
	ASSERT_EQ(::getsockname(listener.Handle(), reinterpret_cast<sockaddr*>(&bound), &boundLength), 0);
	const uint16_t port = ::ntohs(bound.sin_port);

	auto clientResult = Socket::Create(context, AF_INET, SOCK_STREAM, IPPROTO_TCP);
	ASSERT_TRUE(clientResult.IsOk()) << clientResult.Error().What();
	Socket client = std::move(clientResult.Value());

	// accept 와 connect 를 동시에 걸어 둔다 — 어느 쪽도 루프를 독점하지 못해야 둘 다 완료된다.
	auto acceptTask = listener.Accept();
	auto connectTask = client.Connect("127.0.0.1", port);

	acceptTask.Resume();
	connectTask.Resume();

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while ((!acceptTask.IsReady() || !connectTask.IsReady()) && std::chrono::steady_clock::now() < deadline) (void_t)context.RunOnce(std::chrono::milliseconds{ 10 });

	ASSERT_TRUE(connectTask.IsReady()) << "connect 가 완료되지 않았다 — 소켓이 블로킹이면 루프가 정지한다";
	ASSERT_TRUE(acceptTask.IsReady()) << "accept 가 완료되지 않았다 — 소켓이 블로킹이면 루프가 정지한다";

	auto connectResult = connectTask.await_resume();
	ASSERT_TRUE(connectResult.IsOk()) << connectResult.Error().What();

	auto acceptResult = acceptTask.await_resume();
	ASSERT_TRUE(acceptResult.IsOk()) << acceptResult.Error().What();
	Socket accepted = std::move(acceptResult.Value());

	constexpr char payload[] = "reactor-socket-roundtrip";
	constexpr std::size_t length = sizeof(payload) - 1;

	auto sendTask = client.Send(std::span<const ne::byte_t>{ reinterpret_cast<const ne::byte_t*>(payload), length });
	ASSERT_TRUE(Drive(context, sendTask));
	auto sendResult = sendTask.await_resume();
	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();
	EXPECT_EQ(sendResult.Value(), length);

	ne::byte_t buffer[64]{};
	auto receiveTask = accepted.Receive(std::span<ne::byte_t>{ buffer, length });
	ASSERT_TRUE(Drive(context, receiveTask));
	auto receiveResult = receiveTask.await_resume();
	ASSERT_TRUE(receiveResult.IsOk()) << receiveResult.Error().What();
	EXPECT_EQ(receiveResult.Value(), length);
	EXPECT_EQ(std::memcmp(buffer, payload, length), 0);
}

// ── 데이터가 아직 없을 때 Receive 가 루프를 붙잡지 않는다 ──
//
// 블로킹 소켓이면 ::recv 가 데이터를 기다리며 스레드를 잡는다. 그러면 같은 루프에서 뒤이어 제출한
// Send 가 영원히 실행되지 못해 Receive 도 끝나지 않는다 — 논블로킹일 때만 성립하는 시나리오.
TEST(SocketReactorTest, PendingReceiveDoesNotBlockTheLoop)
{
	const WsaScope wsa;
	WsaPollEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto listenerResult = Socket::Create(context, AF_INET, SOCK_STREAM, IPPROTO_TCP);
	ASSERT_TRUE(listenerResult.IsOk());
	Socket listener = std::move(listenerResult.Value());
	ASSERT_TRUE(listener.Bind("127.0.0.1", 0).IsOk());
	ASSERT_TRUE(listener.Listen(1).IsOk());

	sockaddr_in bound{};
	int_t boundLength = static_cast<int_t>(sizeof(bound));
	ASSERT_EQ(::getsockname(listener.Handle(), reinterpret_cast<sockaddr*>(&bound), &boundLength), 0);

	auto clientResult = Socket::Create(context, AF_INET, SOCK_STREAM, IPPROTO_TCP);
	ASSERT_TRUE(clientResult.IsOk());
	Socket client = std::move(clientResult.Value());

	auto acceptTask = listener.Accept();
	auto connectTask = client.Connect("127.0.0.1", ::ntohs(bound.sin_port));
	acceptTask.Resume();
	connectTask.Resume();

	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while ((!acceptTask.IsReady() || !connectTask.IsReady()) && std::chrono::steady_clock::now() < deadline) (void_t)context.RunOnce(std::chrono::milliseconds{ 10 });
	ASSERT_TRUE(acceptTask.IsReady() && connectTask.IsReady());
	ASSERT_TRUE(connectTask.await_resume().IsOk());

	auto acceptResult = acceptTask.await_resume();
	ASSERT_TRUE(acceptResult.IsOk());
	Socket accepted = std::move(acceptResult.Value());

	// 데이터가 없는 상태에서 먼저 Receive 를 제출한다.
	ne::byte_t buffer[16]{};
	auto receiveTask = accepted.Receive(std::span<ne::byte_t>{ buffer, sizeof(buffer) });
	receiveTask.Resume();

	// 루프를 몇 번 돌려도 Receive 는 대기 상태여야 하고, 루프는 살아 있어야 한다.
	for (int_t i = 0; i < 3; ++i) (void_t)context.RunOnce(std::chrono::milliseconds{ 5 });
	EXPECT_FALSE(receiveTask.IsReady());

	// 이제 같은 루프에서 Send 를 제출한다 — 루프가 정지했다면 이 단계가 진전하지 못한다.
	constexpr char payload[] = "unblocked";
	constexpr std::size_t length = sizeof(payload) - 1;
	auto sendTask = client.Send(std::span<const ne::byte_t>{ reinterpret_cast<const ne::byte_t*>(payload), length });
	ASSERT_TRUE(Drive(context, sendTask));
	ASSERT_TRUE(sendTask.await_resume().IsOk());

	deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!receiveTask.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)context.RunOnce(std::chrono::milliseconds{ 10 });

	ASSERT_TRUE(receiveTask.IsReady()) << "대기 중인 Receive 가 완료되지 않았다 — 루프가 블로킹된 상태다";
	auto receiveResult = receiveTask.await_resume();
	ASSERT_TRUE(receiveResult.IsOk()) << receiveResult.Error().What();
	EXPECT_EQ(receiveResult.Value(), length);
	EXPECT_EQ(std::memcmp(buffer, payload, length), 0);
}

// ── 같은 소켓·같은 방향으로 동시에 두 개의 Receive 를 걸어도 둘 다 완료된다 ──
//
// 회귀 대상: 리액터 엔진의 readWaiter/writeWaiter 가 fd 당 하나만 담는 맵이었다. 두 번째 op 이
// 첫 번째를 **덮어써** 그 op 은 pending 에 남은 채 영원히 완료되지 않았다(코루틴 영구 대기 +
// 핸들러 누수). 지금은 fd 당 FIFO 큐이며, 통지 한 번에 여러 op 이 완료될 수 있다.
TEST(SocketReactorTest, TwoConcurrentReceivesOnSameSocketBothComplete)
{
	const WsaScope wsa;
	WsaPollEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto listenerResult = Socket::Create(context, AF_INET, SOCK_STREAM, IPPROTO_TCP);
	ASSERT_TRUE(listenerResult.IsOk());
	Socket listener = std::move(listenerResult.Value());
	ASSERT_TRUE(listener.Bind("127.0.0.1", 0).IsOk());
	ASSERT_TRUE(listener.Listen(1).IsOk());

	auto clientResult = Socket::Create(context, AF_INET, SOCK_STREAM, IPPROTO_TCP);
	ASSERT_TRUE(clientResult.IsOk());
	Socket client = std::move(clientResult.Value());

	auto acceptTask = listener.Accept();
	auto connectTask = client.Connect("127.0.0.1", listener.LocalPort());
	acceptTask.Resume();
	connectTask.Resume();

	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while ((!acceptTask.IsReady() || !connectTask.IsReady()) && std::chrono::steady_clock::now() < deadline) (void_t)context.RunOnce(std::chrono::milliseconds{ 10 });
	ASSERT_TRUE(acceptTask.IsReady() && connectTask.IsReady());
	ASSERT_TRUE(connectTask.await_resume().IsOk());

	auto acceptResult = acceptTask.await_resume();
	ASSERT_TRUE(acceptResult.IsOk());
	Socket accepted = std::move(acceptResult.Value());

	// 데이터가 없는 상태에서 같은 소켓에 Receive 를 **두 개** 제출한다.
	ne::byte_t first[4]{};
	ne::byte_t second[4]{};
	auto firstTask = accepted.Receive(std::span<ne::byte_t>{ first, sizeof(first) });
	auto secondTask = accepted.Receive(std::span<ne::byte_t>{ second, sizeof(second) });
	firstTask.Resume();
	secondTask.Resume();

	for (int_t i = 0; i < 3; ++i) (void_t)context.RunOnce(std::chrono::milliseconds{ 5 });
	EXPECT_FALSE(firstTask.IsReady());
	EXPECT_FALSE(secondTask.IsReady());

	// 두 요청 분량을 한 번에 보낸다 — 큐 방식이면 통지 한 번으로 둘 다 완료될 수 있다.
	constexpr char payload[] = "AAAABBBB";
	auto sendTask = client.Send(std::span<const ne::byte_t>{ reinterpret_cast<const ne::byte_t*>(payload), 8 });
	ASSERT_TRUE(Drive(context, sendTask));
	ASSERT_TRUE(sendTask.await_resume().IsOk());

	deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while ((!firstTask.IsReady() || !secondTask.IsReady()) && std::chrono::steady_clock::now() < deadline) (void_t)context.RunOnce(std::chrono::milliseconds{ 10 });

	ASSERT_TRUE(firstTask.IsReady()) << "첫 Receive 가 완료되지 않았다";
	ASSERT_TRUE(secondTask.IsReady()) << "두 번째 Receive 가 완료되지 않았다 — fd 당 대기 슬롯이 하나면 유실된다";

	auto firstResult = firstTask.await_resume();
	auto secondResult = secondTask.await_resume();
	ASSERT_TRUE(firstResult.IsOk()) << firstResult.Error().What();
	ASSERT_TRUE(secondResult.IsOk()) << secondResult.Error().What();

	// 제출 순서(FIFO)대로 앞 4바이트/뒤 4바이트를 받는다.
	EXPECT_EQ(firstResult.Value(), 4u);
	EXPECT_EQ(secondResult.Value(), 4u);
	EXPECT_EQ(std::memcmp(first, "AAAA", 4), 0);
	EXPECT_EQ(std::memcmp(second, "BBBB", 4), 0);
}

#endif // _WIN32
