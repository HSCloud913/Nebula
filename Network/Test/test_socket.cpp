//
// Created by hscloud on 26. 6. 30.
//

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "Socket/Socket.h"
#include "Coroutine/Task.h"

#if defined(IS_POSIX)
#	include "Engine/Epoll/EpollEngine.h"
#elif defined(_WIN32)
#	include "Engine/Iocp/IocpEngine.h"
#endif

using namespace ne::network;

namespace
{
	// 비동기 Task 를 blocking 으로 구동 — DNS 조회/연결은 워커 스레드에서 완료되므로 여기서 대기.
	template <typename T>
	T RunSync(ne::Task<T> _task)
	{
		_task.Resume();
		while (!_task.IsReady())
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		return _task.await_resume();
	}

	ne::Result<Socket, ne::OsError> CreateTcp(const AddressFamily _family = AddressFamily::IPv4)
	{
		return Socket::Create(_family, SOCK_STREAM, IPPROTO_TCP);
	}

	ne::Result<Socket, ne::OsError> CreateUdp(const AddressFamily _family = AddressFamily::IPv4)
	{
		return Socket::Create(_family, SOCK_DGRAM, IPPROTO_UDP);
	}

#if defined(IS_POSIX)
	using TestEngine = ne::io::EpollEngine;
#elif defined(_WIN32)
	using TestEngine = ne::io::IocpEngine;
#endif

	// Connect(host,port,engine) 은 완료 통지를 RunOnce() 스레드로만 전달하므로 직접 구동해야 한다.
	template <typename T>
	T RunSyncWithEngine(ne::io::IIoEngine& _engine, ne::Task<T> _task,
	                     std::chrono::milliseconds _timeout = std::chrono::seconds(5))
	{
		_task.Resume();

		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while (!_task.IsReady() && std::chrono::steady_clock::now() < deadline)
			_engine.RunOnce(10);

		return _task.await_resume();
	}

	// 로컬 리스너를 열고 실제로 배정된 포트를 반환한다(Socket 은 getsockname 을 노출하지 않으므로 직접 조회).
	ne::Result<uint16_t, ne::OsError> BindEphemeralListener(Socket& _listener)
	{
		if (auto r = _listener.SetReuseAddress(true); r.IsError()) return ne::Result<uint16_t, ne::OsError>::Error(std::move(r.Error()));
		if (auto r = RunSync(_listener.Bind("127.0.0.1", 0)); r.IsError()) return ne::Result<uint16_t, ne::OsError>::Error(std::move(r.Error()));
		if (auto r = _listener.Listen(); r.IsError()) return ne::Result<uint16_t, ne::OsError>::Error(std::move(r.Error()));

		sockaddr_in addr{};
		socklen_t addrLen = sizeof(addr);
		if (::getsockname(_listener.Handle(), reinterpret_cast<sockaddr*>(&addr), &addrLen) != 0)
			return ne::Result<uint16_t, ne::OsError>::Error(ne::OsError{ ne::LastOsError() });

		return ne::Result<uint16_t, ne::OsError>::Ok(::ntohs(addr.sin_port));
	}
}

TEST(SocketTest, CreateTcpSucceeds)
{
	auto result = CreateTcp();
	ASSERT_TRUE(result.IsOk());
	EXPECT_TRUE(result.Value().IsValid());
}

TEST(SocketTest, CreateUdpSucceeds)
{
	auto result = CreateUdp();
	ASSERT_TRUE(result.IsOk());
	EXPECT_TRUE(result.Value().IsValid());
}

TEST(SocketTest, MoveInvalidatesSource)
{
	auto result = CreateTcp();
	ASSERT_TRUE(result.IsOk());

	Socket a = std::move(result.Value());
	EXPECT_TRUE(a.IsValid());

	Socket b = std::move(a);
	EXPECT_FALSE(a.IsValid());
	EXPECT_TRUE(b.IsValid());
}

TEST(SocketTest, SetReuseAddrSucceeds)
{
	auto result = CreateTcp();
	ASSERT_TRUE(result.IsOk());

	auto optResult = result.Value().SetReuseAddress(true);
	EXPECT_TRUE(optResult.IsOk());
}

TEST(SocketTest, BindFailsOnInvalidAddress)
{
	auto result = CreateTcp();
	ASSERT_TRUE(result.IsOk());

	// 존재하지 않는 주소에 바인드하면 실패해야 함.
	auto bindResult = RunSync(result.Value().Bind("999.999.999.999", 0));
	EXPECT_TRUE(bindResult.IsError());
}

TEST(SocketTest, ResolveFamilyDetectsLiterals)
{
	auto v4 = RunSync(Socket::ResolveFamily("127.0.0.1"));
	ASSERT_TRUE(v4.IsOk());
	EXPECT_EQ(v4.Value(), AddressFamily::IPv4);

	auto v6 = RunSync(Socket::ResolveFamily("::1"));
	ASSERT_TRUE(v6.IsOk());
	EXPECT_EQ(v6.Value(), AddressFamily::IPv6);
}

TEST(SocketTest, CreateIpv6Succeeds)
{
	auto result = CreateTcp(AddressFamily::IPv6);
	ASSERT_TRUE(result.IsOk());
	EXPECT_TRUE(result.Value().IsValid());
	EXPECT_EQ(result.Value().Family(), AddressFamily::IPv6);
}

TEST(SocketTest, Ipv6BindAndAcceptRoundTrip)
{
	auto serverResult = CreateTcp(AddressFamily::IPv6);
	ASSERT_TRUE(serverResult.IsOk());
	Socket server = std::move(serverResult.Value());

	ASSERT_TRUE(server.SetReuseAddress(true).IsOk());
	ASSERT_TRUE(RunSync(server.Bind("::1", 0)).IsOk());
	ASSERT_TRUE(server.Listen().IsOk());
}

// ── 비동기 Connect(host,port,engine) ────────────────────────────────────────
// TCP 핸드셰이크는 리스너가 LISTEN 상태이기만 하면 accept() 호출 없이도 백로그에서
// 완료되므로, 여기선 accept 를 부르지 않고 connect 결과만 확인한다.
TEST(SocketTest, ConnectAsyncSucceedsToLocalListener)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());

	auto serverResult = CreateTcp();
	ASSERT_TRUE(serverResult.IsOk());
	Socket server = std::move(serverResult.Value());

	auto portResult = BindEphemeralListener(server);
	ASSERT_TRUE(portResult.IsOk()) << portResult.Error().What();

	auto clientResult = CreateTcp();
	ASSERT_TRUE(clientResult.IsOk());
	Socket client = std::move(clientResult.Value());

	auto connectResult = RunSyncWithEngine(engine, client.Connect("127.0.0.1", portResult.Value(), engine));
	EXPECT_TRUE(connectResult.IsOk()) << connectResult.Error().What();
}

// 핵심 회귀 테스트: 아무도 듣지 않는 포트로의 connect 는 blocking OS 타임아웃(수십 초)까지
// 기다리지 않고 즉시(수백 ms 이내) ECONNREFUSED 로 실패해야 한다 — non-blocking connect +
// SO_ERROR 확인이 실제로 동작하는지 검증.
TEST(SocketTest, ConnectAsyncFailsQuicklyOnRefusedPort)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());

	// 잠깐 리스너를 열어 실제로 비어있던 포트 번호를 얻은 뒤 바로 닫는다 — 그 직후엔
	// 아무도 듣고 있지 않으므로 connect 가 ECONNREFUSED 로 즉시 실패한다.
	uint16_t port = 0;
	{
		auto serverResult = CreateTcp();
		ASSERT_TRUE(serverResult.IsOk());
		Socket server = std::move(serverResult.Value());

		auto portResult = BindEphemeralListener(server);
		ASSERT_TRUE(portResult.IsOk()) << portResult.Error().What();
		port = portResult.Value();
	}

	auto clientResult = CreateTcp();
	ASSERT_TRUE(clientResult.IsOk());
	Socket client = std::move(clientResult.Value());

	const auto start = std::chrono::steady_clock::now();
	auto connectResult = RunSyncWithEngine(engine, client.Connect("127.0.0.1", port, engine), std::chrono::seconds(5));
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

	EXPECT_TRUE(connectResult.IsError());
	EXPECT_LT(elapsed, 2000) << "refused connect must fail quickly, not block for the OS connect timeout";
}
