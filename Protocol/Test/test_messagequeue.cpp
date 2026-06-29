//
// Created by nebula on 24. 11. 3.
//

#include <gtest/gtest.h>
#include <string>
#include <thread>

#include "Ipc/MessageQueue.h"

using ne::protocol::Ipc::MessageQueue;

namespace
{
	std::span<const std::byte> AsBytes(const std::string& _string)
	{
		return std::span(reinterpret_cast<const std::byte*>(_string.data()), _string.size());
	}

	std::string AsString(const std::span<const std::byte> _bytes)
	{
		return std::string(reinterpret_cast<const ne::char_t*>(_bytes.data()), _bytes.size());
	}
}

TEST(MessageQueueTest, SendReceiveRoundTrip)
{
	auto server = MessageQueue("nebula-mq-test-roundtrip");
	auto serverThread = std::thread([&server] { server.Listen(); });

	auto client = MessageQueue("nebula-mq-test-roundtrip");
	client.Connect();
	serverThread.join();

	const auto message = std::string("nebula-ipc-message-queue");
	client.Send(AsBytes(message));

	const auto received = server.Receive();
	EXPECT_EQ(AsString(received), message);
}

TEST(MessageQueueTest, PreservesMessageBoundaries)
{
	auto server = MessageQueue("nebula-mq-test-boundaries");
	auto serverThread = std::thread([&server] { server.Listen(); });

	auto client = MessageQueue("nebula-mq-test-boundaries");
	client.Connect();
	serverThread.join();

	client.Send(AsBytes(std::string("first")));
	client.Send(AsBytes(std::string("second")));

	EXPECT_EQ(AsString(server.Receive()), "first");
	EXPECT_EQ(AsString(server.Receive()), "second");
}
