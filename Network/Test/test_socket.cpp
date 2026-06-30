//
// Created by hscloud on 26. 6. 30.
//

#include <gtest/gtest.h>
#include "Socket/Socket.h"

using namespace ne::network;

TEST(SocketTest, CreateTcpSucceeds)
{
	auto result = Socket::CreateTcp();
	ASSERT_TRUE(result.IsOk());
	EXPECT_TRUE(result.Value().IsValid());
}

TEST(SocketTest, CreateUdpSucceeds)
{
	auto result = Socket::CreateUdp();
	ASSERT_TRUE(result.IsOk());
	EXPECT_TRUE(result.Value().IsValid());
}

TEST(SocketTest, MoveInvalidatesSource)
{
	auto result = Socket::CreateTcp();
	ASSERT_TRUE(result.IsOk());

	Socket a = std::move(result.Value());
	EXPECT_TRUE(a.IsValid());

	Socket b = std::move(a);
	EXPECT_FALSE(a.IsValid());
	EXPECT_TRUE(b.IsValid());
}

TEST(SocketTest, SetReuseAddrSucceeds)
{
	auto result = Socket::CreateTcp();
	ASSERT_TRUE(result.IsOk());

	auto optResult = result.Value().SetReuseAddr(true);
	EXPECT_TRUE(optResult.IsOk());
}

TEST(SocketTest, BindFailsOnInvalidAddress)
{
	auto result = Socket::CreateTcp();
	ASSERT_TRUE(result.IsOk());

	// 존재하지 않는 주소에 바인드하면 실패해야 함.
	auto bindResult = result.Value().Bind("999.999.999.999", 0);
	EXPECT_TRUE(bindResult.IsError());
}
