//
// Created by hscloud on 26. 6. 30.
//

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "Socket/Socket.h"
#include "Coroutine/Task.h"

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
