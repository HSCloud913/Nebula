//
// Created by nebula on 24. 11. 3.
//

#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

#include "../../Ipc/Pipe.h"

using ne::ipc::Pipe;

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

TEST(PipeTest, ListenConnectReadWrite)
{
	auto server = Pipe("nebula-pipe-test-readwrite");
	auto serverThread = std::thread([&server] { server.Listen(); });

	auto client = Pipe("nebula-pipe-test-readwrite");
	client.Connect();
	serverThread.join();

	const auto toServer = std::string("nebula-ipc-pipe-to-server");
	ASSERT_TRUE(client.Write(AsBytes(toServer)));

	auto serverBuffer = std::vector<std::byte>(64);
	const auto serverReceived = server.Read(serverBuffer);
	ASSERT_GE(serverReceived, 0);
	serverBuffer.resize(serverReceived);
	EXPECT_EQ(AsString(serverBuffer), toServer);

	const auto toClient = std::string("nebula-ipc-pipe-to-client");
	ASSERT_TRUE(server.Write(AsBytes(toClient)));

	auto clientBuffer = std::vector<std::byte>(64);
	const auto clientReceived = client.Read(clientBuffer);
	ASSERT_GE(clientReceived, 0);
	clientBuffer.resize(clientReceived);
	EXPECT_EQ(AsString(clientBuffer), toClient);
}
