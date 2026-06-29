//
// Created by nebula on 24. 11. 3.
//

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "Socket/UdpSocket.h"

using ne::protocol::UdpSocket;

namespace
{
	ne::int_t GetBoundPort(const UdpSocket& _socket)
	{
		auto address = sockaddr_in{};
		auto addressLength = static_cast<socklen_t>(sizeof(address));
		::getsockname(_socket.GetHandle(), reinterpret_cast<sockaddr*>(&address), &addressLength);
		return ::ntohs(address.sin_port);
	}

	sockaddr_in MakeLoopbackAddress(const ne::int_t _port)
	{
		auto address = sockaddr_in{};
		address.sin_family = AF_INET;
		address.sin_port = ::htons(static_cast<u_short>(_port));
		address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
		return address;
	}

	std::span<const std::byte> AsBytes(const std::string& _string)
	{
		return std::span(reinterpret_cast<const std::byte*>(_string.data()), _string.size());
	}

	std::string AsString(const std::span<const std::byte> _bytes)
	{
		return std::string(reinterpret_cast<const ne::char_t*>(_bytes.data()), _bytes.size());
	}
}

TEST(UdpSocketTest, ConnectedReadWrite)
{
	auto server = UdpSocket("127.0.0.1", 0);
	server.Bind();
	const auto port = GetBoundPort(server);

	auto client = UdpSocket("127.0.0.1", port);
	client.Connect();

	const auto message = std::string("nebula-udp-connected");
	EXPECT_TRUE(client.Write(AsBytes(message)));

	auto fromAddress = sockaddr_in{};
	auto fromAddressLength = static_cast<socklen_t>(sizeof(fromAddress));
	auto buffer = std::vector<std::byte>(64);
	const auto received = server.ReadFrom(buffer, reinterpret_cast<sockaddr*>(&fromAddress), &fromAddressLength);

	ASSERT_GE(received, 0);
	buffer.resize(received);
	EXPECT_EQ(AsString(buffer), message);
}

TEST(UdpSocketTest, SendToReceiveFrom)
{
	auto server = UdpSocket("127.0.0.1", 0);
	server.Bind();
	const auto port = GetBoundPort(server);

	auto client = UdpSocket("0.0.0.0", 0);
	client.Bind();

	auto destination = MakeLoopbackAddress(port);

	const auto message = std::string("nebula-udp-datagram");
	EXPECT_TRUE(client.WriteTo(AsBytes(message), reinterpret_cast<sockaddr*>(&destination), sizeof(destination)));

	auto buffer = std::vector<std::byte>(64);
	const auto received = server.ReadFrom(buffer, nullptr, nullptr);

	ASSERT_GE(received, 0);
	buffer.resize(received);
	EXPECT_EQ(AsString(buffer), message);
}

TEST(UdpSocketTest, EmptyDatagramIsNotTreatedAsError)
{
	auto server = UdpSocket("127.0.0.1", 0);
	server.Bind();
	const auto port = GetBoundPort(server);

	auto client = UdpSocket("127.0.0.1", port);
	client.Connect();

	EXPECT_TRUE(client.Write(std::span<const std::byte>{}));

	auto buffer = std::vector<std::byte>(64);
	const auto received = server.ReadFrom(buffer, nullptr, nullptr);

	EXPECT_EQ(received, 0);
}
