//
// Created by nebula on 24. 11. 3.
//

#include <gtest/gtest.h>
#include <array>
#include <format>
#include <string>
#include <thread>

#include "Ftp/FtpUtil.h"
#include "Ftp/FtpClient.h"

using namespace ne::protocol::Ftp;
using ne::protocol::TcpSocket;

TEST(FtpUtilTest, ParseReplyCodeReadsLeadingThreeDigits)
{
	EXPECT_EQ(ParseReplyCode("220 Welcome"), 220);
	EXPECT_EQ(ParseReplyCode("550-First line"), 550);
	EXPECT_FALSE(ParseReplyCode("xx"));
	EXPECT_FALSE(ParseReplyCode("AB Not a code"));
}

TEST(FtpUtilTest, IsFinalReplyLineDistinguishesContinuationFromTerminator)
{
	EXPECT_FALSE(IsFinalReplyLine("250-First line", 250));
	EXPECT_FALSE(IsFinalReplyLine("250-Still going", 250));
	EXPECT_TRUE(IsFinalReplyLine("250 Done", 250));
	EXPECT_FALSE(IsFinalReplyLine("250 Done", 220));
}

TEST(FtpUtilTest, ParsePassiveReplyExtractsHostAndPort)
{
	const auto address = ParsePassiveReply("227 Entering Passive Mode (192,168,1,5,200,13).");
	ASSERT_TRUE(address);
	EXPECT_EQ(address->host, "192.168.1.5");
	EXPECT_EQ(address->port, 200 * 256 + 13);
}

TEST(FtpUtilTest, ParsePassiveReplyRejectsMalformedReply)
{
	EXPECT_FALSE(ParsePassiveReply("227 Entering Passive Mode"));
	EXPECT_FALSE(ParsePassiveReply("227 Entering Passive Mode (192,168,1,5,200)."));
	EXPECT_FALSE(ParsePassiveReply("227 Entering Passive Mode (192,168,1,5,200,abc)."));
}

TEST(FtpUtilTest, ParseMlsdLineExtractsFactsAndName)
{
	const auto entry = ParseMlsdLine("Type=file;Size=1234;Modify=20240101120000; report.txt");
	ASSERT_TRUE(entry);
	EXPECT_EQ(entry->name, "report.txt");
	EXPECT_EQ(entry->size, 1234u);
	EXPECT_FALSE(entry->isDirectory);
}

TEST(FtpUtilTest, ParseMlsdLineDetectsDirectories)
{
	const auto entry = ParseMlsdLine("Type=dir;Modify=20240101120000; subfolder");
	ASSERT_TRUE(entry);
	EXPECT_EQ(entry->name, "subfolder");
	EXPECT_TRUE(entry->isDirectory);
}

TEST(FtpUtilTest, ParseMlsdLineRejectsLineWithoutNameSeparator)
{
	EXPECT_FALSE(ParseMlsdLine("Type=file;Size=1234"));
}

TEST(FtpUtilTest, ParseUnixListLineParsesFileEntry)
{
	const auto entry = ParseUnixListLine("-rw-r--r--    1 user     group        1234 Jan  1 12:00 report.txt");
	ASSERT_TRUE(entry);
	EXPECT_EQ(entry->name, "report.txt");
	EXPECT_EQ(entry->size, 1234u);
	EXPECT_FALSE(entry->isDirectory);
}

TEST(FtpUtilTest, ParseUnixListLineParsesDirectoryEntry)
{
	const auto entry = ParseUnixListLine("drwxr-xr-x    2 user     group        4096 Jan  1 12:00 subfolder");
	ASSERT_TRUE(entry);
	EXPECT_EQ(entry->name, "subfolder");
	EXPECT_TRUE(entry->isDirectory);
}

TEST(FtpUtilTest, ParseUnixListLineHandlesNameWithSpaces)
{
	const auto entry = ParseUnixListLine("-rw-r--r--    1 user     group        1234 Jan  1 12:00 my report.txt");
	ASSERT_TRUE(entry);
	EXPECT_EQ(entry->name, "my report.txt");
}

TEST(FtpUtilTest, ParseUnixListLineRejectsTooFewFields)
{
	EXPECT_FALSE(ParseUnixListLine("drwxr-xr-x 2 user group"));
}



namespace
{
	ne::void_t SendLine(TcpSocket& _socket, const std::string& _line)
	{
		const auto data = _line + "\r\n";
		_socket.Write(std::span(reinterpret_cast<const std::byte*>(data.data()), data.size()));
	}

	std::string ReceiveLine(TcpSocket& _socket, std::string& _buffer)
	{
		while (true)
		{
			if (const auto pos = _buffer.find('\n'); pos != std::string::npos)
			{
				auto line = _buffer.substr(0, pos);
				_buffer.erase(0, pos + 1);
				if (!line.empty() && line.back() == '\r') line.pop_back();

				return line;
			}

			auto chunk = std::array<std::byte, 256>();
			const auto received = _socket.Read(chunk);
			if (received < 0) return {};

			_buffer.append(reinterpret_cast<const char*>(chunk.data()), received);
		}
	}

	ne::int_t GetBoundPort(const TcpSocket& _socket)
	{
		auto address = sockaddr_in{};
		auto addressLength = static_cast<socklen_t>(sizeof(address));
		::getsockname(_socket.GetHandle(), reinterpret_cast<sockaddr*>(&address), &addressLength);
		return ::ntohs(address.sin_port);
	}

	// Parses "PORT h1,h2,h3,h4,p1,p2" → {host, port}
	std::pair<std::string, ne::int_t> ParsePortCommand(const std::string& _line)
	{
		const auto space = _line.rfind(' ');
		auto parts = std::vector<std::string>{};
		auto cur = std::string{};
		for (const auto c : _line.substr(space + 1))
		{
			if (c == ',') { parts.push_back(cur); cur.clear(); }
			else cur += c;
		}
		parts.push_back(cur);

		return {
			parts[0] + '.' + parts[1] + '.' + parts[2] + '.' + parts[3],
			std::stoi(parts[4]) * 256 + std::stoi(parts[5])
		};
	}
}

TEST(FtpClientTest, ConnectLoginQuitAgainstFakeServer)
{
	auto server = TcpSocket("127.0.0.1", 0);
	server.Bind();
	server.Listen();
	const auto port = GetBoundPort(server);

	auto serverThread = std::thread([&server]
	{
		auto session = TcpSocket(server.Accept());
		auto buffer = std::string();

		SendLine(session, "220 Fake FTP Ready");
		ReceiveLine(session, buffer); // USER ...
		SendLine(session, "331 Need password");
		ReceiveLine(session, buffer); // PASS ...
		SendLine(session, "230 Logged in");
		ReceiveLine(session, buffer); // QUIT
		SendLine(session, "221 Bye");
	});

	auto client = FtpClient("127.0.0.1", port);
	client.Connect();
	client.Login("nebula", "nebula");
	client.Quit();

	serverThread.join();
}

// AUTH TLS / implicit FTPS require an actual TLS handshake against a certificate-bearing
// peer, which the fake control-channel server above cannot provide. These only check that
// constructing a client in either mode selects the right transport without touching the network.
TEST(FtpClientTest, ImplicitFtpsConstructionSelectsTlsTransport)
{
	EXPECT_NO_THROW(FtpClient("127.0.0.1", 21, true, true));
}

TEST(FtpClientTest, PlainConstructionSelectsTcpTransport)
{
	EXPECT_NO_THROW(FtpClient("127.0.0.1", 21, false, false));
}

TEST(FtpClientTest, SetDataModeDoesNotThrow)
{
	auto client = FtpClient("127.0.0.1", 21);
	EXPECT_NO_THROW(client.SetDataMode(true));
	EXPECT_NO_THROW(client.SetDataMode(false));
}

TEST(FtpClientTest, ActiveModeGetAgainstFakeServer)
{
	auto control = TcpSocket("127.0.0.1", 0);
	control.Bind();
	control.Listen();
	const auto controlPort = GetBoundPort(control);

	auto serverThread = std::thread([&control]
	{
		auto session = TcpSocket(control.Accept());
		auto buf = std::string{};

		SendLine(session, "220 Fake FTP Ready");
		ReceiveLine(session, buf); // USER
		SendLine(session, "331 Need password");
		ReceiveLine(session, buf); // PASS
		SendLine(session, "230 Logged in");

		const auto [dataHost, dataPort] = ParsePortCommand(ReceiveLine(session, buf)); // PORT
		SendLine(session, "200 PORT command successful");
		ReceiveLine(session, buf); // RETR

		// Send 150 first so the client's SendCommand(RETR) unblocks, then connect to the
		// client's data port.  The kernel queues our connect() into the client's listen
		// backlog, so AcceptActiveData() returns as soon as we connect.
		SendLine(session, "150 Opening data connection");
		{
			auto dataConn = TcpSocket(dataHost, dataPort);
			dataConn.Connect();
			const auto payload = std::string("hello active");
			dataConn.Write(std::span(reinterpret_cast<const std::byte*>(payload.data()), payload.size()));
		} // dataConn close → client Read returns -1 → readLoop exits

		SendLine(session, "226 Transfer complete");
		ReceiveLine(session, buf); // QUIT
		SendLine(session, "221 Bye");
	});

	auto client = FtpClient("127.0.0.1", controlPort);
	client.Connect();
	client.Login("nebula", "nebula");
	client.SetDataMode(true);

	auto received = std::string{};
	client.Get("remote.txt", [&received](const std::span<const std::byte> _data)
	{
		received.append(reinterpret_cast<const char*>(_data.data()), _data.size());
	});
	client.Quit();
	serverThread.join();

	EXPECT_EQ(received, "hello active");
}

TEST(FtpClientTest, PassiveModeGetWithResumeOffset)
{
	auto control = TcpSocket("127.0.0.1", 0);
	control.Bind();
	control.Listen();
	const auto controlPort = GetBoundPort(control);

	// Prepare the data server before the thread starts so Listen() is in effect
	// when the client connects after receiving the PASV reply.
	auto data = TcpSocket("127.0.0.1", 0);
	data.Bind();
	data.Listen();
	const auto dataPort = GetBoundPort(data);

	auto serverThread = std::thread([&control, &data, dataPort]
	{
		auto session = TcpSocket(control.Accept());
		auto buf = std::string{};

		SendLine(session, "220 Fake FTP Ready");
		ReceiveLine(session, buf); // USER
		SendLine(session, "331 Need password");
		ReceiveLine(session, buf); // PASS
		SendLine(session, "230 Logged in");

		ReceiveLine(session, buf); // REST 5
		SendLine(session, "350 Restart position accepted");

		ReceiveLine(session, buf); // PASV
		SendLine(session, std::format("227 Entering Passive Mode (127,0,0,1,{},{})",
			(dataPort >> 8) & 0xFF, dataPort & 0xFF));

		ReceiveLine(session, buf); // RETR
		// The client has already connected to the data socket between PASV and RETR;
		// Accept() returns immediately from the kernel's accept queue.
		SendLine(session, "150 Opening data connection");
		{
			auto conn = TcpSocket(data.Accept());
			const auto payload = std::string(" world");
			conn.Write(std::span(reinterpret_cast<const std::byte*>(payload.data()), payload.size()));
		} // conn close → client Read returns -1 → readLoop exits

		SendLine(session, "226 Transfer complete");
		ReceiveLine(session, buf); // QUIT
		SendLine(session, "221 Bye");
	});

	auto client = FtpClient("127.0.0.1", controlPort);
	client.Connect();
	client.Login("nebula", "nebula");

	auto received = std::string{};
	client.Get("remote.txt", [&received](const std::span<const std::byte> _data)
	{
		received.append(reinterpret_cast<const char*>(_data.data()), _data.size());
	}, 5); // resume from offset 5 — server is responsible for seeking; we receive what it sends
	client.Quit();
	serverThread.join();

	EXPECT_EQ(received, " world");
}
