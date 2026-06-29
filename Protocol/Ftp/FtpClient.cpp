//
// Created by nebula on 24. 5. 29.
//

#include "FtpClient.h"

#include <array>

#include "FtpUtil.h"
#include "Exception.h"
#if defined(_WIN32)
#	include <ws2tcpip.h>
#elif defined(IS_POSIX)
#	include <netinet/in.h>
#	include <arpa/inet.h>
#endif

BEGIN_NS(ne::protocol::Ftp)
	FtpClient::FtpClient(const string_view_t _host, const int_t _port, const bool_t _isTlsEncrypted, const bool_t _isImplicit)
		: host(_host)
		, controlSocket(MakeControlSocket(string_view_t(host), _port, _isTlsEncrypted, _isImplicit))
		, isTlsEncrypted(_isTlsEncrypted)
	{
	}

	FtpClient::~FtpClient() = default;

	FtpClient::EitherSocket FtpClient::MakeControlSocket(const string_view_t _host, const int_t _port, const bool_t _isTlsEncrypted, const bool_t _isImplicit)
	{
		if (_isTlsEncrypted && _isImplicit) return TlsSocket(_host, _port);

		return TcpSocket(_host, _port);
	}



	void_t FtpClient::Connect()
	{
		std::visit([](auto& _socket) { _socket.Connect(); }, controlSocket);

		if (const auto reply = ReadReply(); reply.code != 220)
		{
			throw ne::Exception("[FtpClient/Connect]", std::format("Unexpected welcome reply ({})", reply.code));
		}

		if (isTlsEncrypted && std::holds_alternative<TcpSocket>(controlSocket))
		{
			if (const auto reply = SendCommand("AUTH TLS"); reply.code != 234 && reply.code != 200)
			{
				throw ne::Exception("[FtpClient/Connect]", std::format("AUTH TLS command failed ({})", reply.code));
			}

			const auto rawHandle = std::get<TcpSocket>(controlSocket).ReleaseHandle();
			controlSocket = TlsSocket(rawHandle, string_view_t(host));
			std::get<TlsSocket>(controlSocket).Handshake();
		}

		if (isTlsEncrypted)
		{
			if (const auto reply = SendCommand("PBSZ 0"); reply.code != 200)
			{
				throw ne::Exception("[FtpClient/Connect]", std::format("PBSZ command failed ({})", reply.code));
			}

			if (const auto reply = SendCommand("PROT P"); reply.code != 200)
			{
				throw ne::Exception("[FtpClient/Connect]", std::format("PROT command failed ({})", reply.code));
			}
		}
	}

	void_t FtpClient::Login(const string_view_t _user, const string_view_t _password)
	{
		if (const auto reply = SendCommand(std::format("USER {}", _user)); reply.code == 230)
		{
			return;
		}
		else if (reply.code != 331)
		{
			throw ne::Exception("[FtpClient/Login]", std::format("USER command failed ({})", reply.code));
		}

		if (const auto reply = SendCommand(std::format("PASS {}", _password)); reply.code != 230)
		{
			throw ne::Exception("[FtpClient/Login]", std::format("PASS command failed ({})", reply.code));
		}
	}

	void_t FtpClient::Quit()
	{
		[[maybe_unused]] const auto reply = SendCommand("QUIT");
	}



	void_t FtpClient::SetTransferType(const TransferType _type)
	{
		if (const auto reply = SendCommand(_type == TransferType::Ascii ? "TYPE A" : "TYPE I"); reply.code != 200)
		{
			throw ne::Exception("[FtpClient/SetTransferType]", std::format("TYPE command failed ({})", reply.code));
		}
	}

	string_t FtpClient::Pwd()
	{
		const auto reply = SendCommand("PWD");
		if (reply.code != 257)
		{
			throw ne::Exception("[FtpClient/Pwd]", std::format("PWD command failed ({})", reply.code));
		}

		const auto first = reply.message.find('"');
		const auto last = reply.message.find('"', first + 1);
		if (first == string_t::npos || last == string_t::npos)
		{
			throw ne::Exception("[FtpClient/Pwd]", std::format("Failed to parse PWD reply ({})", reply.message));
		}

		return reply.message.substr(first + 1, last - first - 1);
	}

	void_t FtpClient::Cwd(const string_view_t _path)
	{
		if (const auto reply = SendCommand(std::format("CWD {}", _path)); reply.code != 250)
		{
			throw ne::Exception("[FtpClient/Cwd]", std::format("CWD command failed ({})", reply.code));
		}
	}

	void_t FtpClient::Mkd(const string_view_t _path)
	{
		if (const auto reply = SendCommand(std::format("MKD {}", _path)); reply.code != 257)
		{
			throw ne::Exception("[FtpClient/Mkd]", std::format("MKD command failed ({})", reply.code));
		}
	}

	void_t FtpClient::Rmd(const string_view_t _path)
	{
		if (const auto reply = SendCommand(std::format("RMD {}", _path)); reply.code != 250)
		{
			throw ne::Exception("[FtpClient/Rmd]", std::format("RMD command failed ({})", reply.code));
		}
	}

	void_t FtpClient::Dele(const string_view_t _path)
	{
		if (const auto reply = SendCommand(std::format("DELE {}", _path)); reply.code != 250)
		{
			throw ne::Exception("[FtpClient/Dele]", std::format("DELE command failed ({})", reply.code));
		}
	}

	void_t FtpClient::Rename(const string_view_t _from, const string_view_t _to)
	{
		if (const auto reply = SendCommand(std::format("RNFR {}", _from)); reply.code != 350)
		{
			throw ne::Exception("[FtpClient/Rename]", std::format("RNFR command failed ({})", reply.code));
		}

		if (const auto reply = SendCommand(std::format("RNTO {}", _to)); reply.code != 250)
		{
			throw ne::Exception("[FtpClient/Rename]", std::format("RNTO command failed ({})", reply.code));
		}
	}



	std::vector<Entry> FtpClient::List(const string_view_t _path)
	{
		auto parseRaw = [&](const string_t& _raw, bool_t _useMlsd) -> std::vector<Entry>
		{
			auto lines = std::vector<string_t>();
			StringFormat::Tokenize(_raw, string_t("\n"), lines, TokenizeOption::TRIM);
			std::erase_if(lines, [](const string_t& _line) { return _line.empty(); });

			auto entries = std::vector<Entry>();
			for (const auto& line : lines)
			{
				auto entry = _useMlsd ? ParseMlsdLine(line) : ParseUnixListLine(line);
				if (!entry || entry->name == "." || entry->name == "..") continue;
				entries.push_back(std::move(*entry));
			}
			return entries;
		};

		auto readAll = [&](EitherSocket& _ds) -> string_t
		{
			auto raw = string_t();
			auto buffer = std::array<std::byte, 4096>();
			while (true)
			{
				const auto received = ReadEither(_ds, buffer);
				if (received < 0) break;
				raw.append(reinterpret_cast<const char_t*>(buffer.data()), static_cast<size_t>(received));
			}
			if (const auto finalReply = ReadReply(); finalReply.code != 226 && finalReply.code != 250)
				throw ne::Exception("[FtpClient/List]", std::format("Directory listing did not complete successfully ({})", finalReply.code));
			return raw;
		};

		const auto mlsdCmd = _path.empty() ? string_t("MLSD") : std::format("MLSD {}", _path);
		const auto listCmd = _path.empty() ? string_t("LIST") : std::format("LIST {}", _path);

		if (isActive)
		{
			PrepareActivePort();
			auto reply = SendCommand(mlsdCmd);
			const auto useMlsd = !(reply.code == 500 || reply.code == 502);

			if (!useMlsd)
			{
				PrepareActivePort(); // close unused listen fd, send new PORT
				reply = SendCommand(listCmd);
			}

			if (reply.code != 150 && reply.code != 125)
				throw ne::Exception("[FtpClient/List]", std::format("Directory listing command failed ({})", reply.code));

			auto dataSocket = AcceptActiveData();
			return parseRaw(readAll(dataSocket), useMlsd);
		}
		else
		{
			auto dataSocket = OpenPassiveDataConnection();
			auto reply = SendCommand(mlsdCmd);
			auto useMlsd = true;

			if (reply.code == 500 || reply.code == 502)
			{
				useMlsd = false;
				dataSocket = OpenPassiveDataConnection();
				reply = SendCommand(listCmd);
			}

			if (reply.code != 150 && reply.code != 125)
				throw ne::Exception("[FtpClient/List]", std::format("Directory listing command failed ({})", reply.code));

			return parseRaw(readAll(dataSocket), useMlsd);
		}
	}

	void_t FtpClient::Get(const string_view_t _remotePath, const std::function<void_t(std::span<const std::byte>)>& _sink, const ulonglong_t _offset)
	{
		if (_offset > 0)
		{
			if (const auto reply = SendCommand(std::format("REST {}", _offset)); reply.code != 350)
				throw ne::Exception("[FtpClient/Get]", std::format("REST command failed ({})", reply.code));
		}

		auto readLoop = [&](EitherSocket& _ds)
		{
			auto buffer = std::array<std::byte, 8192>();
			while (true)
			{
				const auto received = ReadEither(_ds, buffer);
				if (received < 0) break;
				_sink(std::span(buffer).first(received));
			}
			if (const auto finalReply = ReadReply(); finalReply.code != 226 && finalReply.code != 250)
				throw ne::Exception("[FtpClient/Get]", std::format("Transfer did not complete successfully ({})", finalReply.code));
		};

		if (isActive)
		{
			PrepareActivePort();
			if (const auto reply = SendCommand(std::format("RETR {}", _remotePath)); reply.code != 150 && reply.code != 125)
				throw ne::Exception("[FtpClient/Get]", std::format("RETR command failed ({})", reply.code));
			auto dataSocket = AcceptActiveData();
			readLoop(dataSocket);
		}
		else
		{
			auto dataSocket = OpenPassiveDataConnection();
			if (const auto reply = SendCommand(std::format("RETR {}", _remotePath)); reply.code != 150 && reply.code != 125)
				throw ne::Exception("[FtpClient/Get]", std::format("RETR command failed ({})", reply.code));
			readLoop(dataSocket);
		}
	}

	void_t FtpClient::Put(const string_view_t _remotePath, const std::span<const std::byte> _data, const ulonglong_t _offset)
	{
		if (_offset > 0)
		{
			if (const auto reply = SendCommand(std::format("REST {}", _offset)); reply.code != 350)
				throw ne::Exception("[FtpClient/Put]", std::format("REST command failed ({})", reply.code));
		}

		auto writeAndFinish = [&](EitherSocket& _ds)
		{
			WriteEither(_ds, _data);
		};

		if (isActive)
		{
			PrepareActivePort();
			if (const auto reply = SendCommand(std::format("STOR {}", _remotePath)); reply.code != 150 && reply.code != 125)
				throw ne::Exception("[FtpClient/Put]", std::format("STOR command failed ({})", reply.code));
			{
				auto dataSocket = AcceptActiveData();
				writeAndFinish(dataSocket);
			} // dataSocket closes → server sees end-of-transfer
		}
		else
		{
			{
				auto dataSocket = OpenPassiveDataConnection();
				if (const auto reply = SendCommand(std::format("STOR {}", _remotePath)); reply.code != 150 && reply.code != 125)
					throw ne::Exception("[FtpClient/Put]", std::format("STOR command failed ({})", reply.code));
				writeAndFinish(dataSocket);
			} // dataSocket closes → server sees end-of-transfer
		}

		if (const auto finalReply = ReadReply(); finalReply.code != 226 && finalReply.code != 250)
			throw ne::Exception("[FtpClient/Put]", std::format("Transfer did not complete successfully ({})", finalReply.code));
	}



	Reply FtpClient::SendCommand(const string_view_t _command)
	{
		const auto data = std::format("{}\r\n", _command);
		std::visit([&](auto& _socket) { _socket.Write(std::span(reinterpret_cast<const std::byte*>(data.data()), data.size())); }, controlSocket);

		return ReadReply();
	}

	Reply FtpClient::ReadReply()
	{
		auto line = ReadLine();
		const auto code = ParseReplyCode(line);
		if (!code)
		{
			throw ne::Exception("[FtpClient/ReadReply]", std::format("Malformed FTP reply ({})", line));
		}

		auto message = line;
		if (line.size() >= 4 && line[3] == '-')
		{
			while (!IsFinalReplyLine(line, *code))
			{
				line = ReadLine();
				message += "\n" + line;
			}
		}

		return Reply{ .code = *code, .message = std::move(message) };
	}

	string_t FtpClient::ReadLine()
	{
		while (true)
		{
			if (const auto pos = recvBuffer.find('\n'); pos != string_t::npos)
			{
				auto line = recvBuffer.substr(0, pos);
				recvBuffer.erase(0, pos + 1);
				if (!line.empty() && line.back() == '\r') line.pop_back();

				return line;
			}

			auto chunk = std::array<std::byte, 512>();
			const auto received = std::visit([&](auto& _socket) { return _socket.Read(chunk); }, controlSocket);
			if (received < 0)
			{
				throw ne::Exception("[FtpClient/ReadLine]", "The control connection closed unexpectedly");
			}

			recvBuffer.append(reinterpret_cast<const char_t*>(chunk.data()), received);
		}
	}

	FtpClient::EitherSocket FtpClient::OpenPassiveDataConnection()
	{
		const auto reply = SendCommand("PASV");
		if (reply.code != 227)
			throw ne::Exception("[FtpClient/OpenPassiveDataConnection]", std::format("PASV command failed ({})", reply.code));

		const auto address = ParsePassiveReply(reply.message);
		if (!address)
			throw ne::Exception("[FtpClient/OpenPassiveDataConnection]", std::format("Failed to parse PASV reply ({})", reply.message));

		auto dataSocket = TcpSocket(address->host, address->port);
		dataSocket.Connect();

		if (!isTlsEncrypted) return dataSocket;

		const auto rawHandle = dataSocket.ReleaseHandle();
		auto tlsDataSocket = TlsSocket(rawHandle, string_view_t(host));
		tlsDataSocket.Handshake();

		return tlsDataSocket;
	}

	void_t FtpClient::PrepareActivePort()
	{
		// Close any pending listen socket from a previous (possibly failed) transfer
		activeListenHandle = SocketHandle{};

		const auto listenFd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#if defined(_WIN32)
		if (listenFd == INVALID_SOCKET)
			throw ne::Exception("[FtpClient/PrepareActivePort]", std::format("Failed to create listen socket (error: {})", GetSocketError()));
#elif defined(IS_POSIX)
		if (listenFd == -1)
			throw ne::Exception("[FtpClient/PrepareActivePort]", std::format("Failed to create listen socket (error: {})", GetSocketError()));
#endif

		sockaddr_in localAddr{};
		localAddr.sin_family = AF_INET;
		localAddr.sin_addr.s_addr = INADDR_ANY;
		localAddr.sin_port = 0;

		if (::bind(listenFd, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr)) != 0)
		{
#if defined(_WIN32)
			::closesocket(listenFd);
#elif defined(IS_POSIX)
			::close(listenFd);
#endif
			throw ne::Exception("[FtpClient/PrepareActivePort]", std::format("Failed to bind active listen socket (error: {})", GetSocketError()));
		}

		::listen(listenFd, 1);

		// Get the OS-assigned port
		socklen_t boundLen = sizeof(localAddr);
		::getsockname(listenFd, reinterpret_cast<sockaddr*>(&localAddr), &boundLen);
		const int_t boundPort = ntohs(localAddr.sin_port);

		// Get our local IP from the control connection
		const socket_t controlFd = std::visit([](const auto& _s) -> socket_t { return _s.GetHandle(); }, controlSocket);
		sockaddr_in controlAddr{};
		socklen_t controlLen = sizeof(controlAddr);
		::getsockname(controlFd, reinterpret_cast<sockaddr*>(&controlAddr), &controlLen);
		const uint32_t localIp = ntohl(controlAddr.sin_addr.s_addr);

		activeListenHandle = SocketHandle(listenFd);

		const auto reply = SendCommand(std::format("PORT {},{},{},{},{},{}",
			(localIp >> 24) & 0xFFu,
			(localIp >> 16) & 0xFFu,
			(localIp >>  8) & 0xFFu,
			 localIp        & 0xFFu,
			(boundPort >> 8) & 0xFFu,
			 boundPort       & 0xFFu));

		if (reply.code != 200)
		{
			activeListenHandle = SocketHandle{};
			throw ne::Exception("[FtpClient/PrepareActivePort]", std::format("PORT command failed ({})", reply.code));
		}
	}

	FtpClient::EitherSocket FtpClient::AcceptActiveData()
	{
		const socket_t listenFd = activeListenHandle.Get();
		const socket_t dataFd = ::accept(listenFd, nullptr, nullptr);
		activeListenHandle = SocketHandle{};

#if defined(_WIN32)
		if (dataFd == INVALID_SOCKET)
			throw ne::Exception("[FtpClient/AcceptActiveData]", std::format("Failed to accept active data connection (error: {})", GetSocketError()));
#elif defined(IS_POSIX)
		if (dataFd == -1)
			throw ne::Exception("[FtpClient/AcceptActiveData]", std::format("Failed to accept active data connection (error: {})", GetSocketError()));
#endif

		if (!isTlsEncrypted) return EitherSocket(TcpSocket(dataFd));

		auto tlsSocket = TlsSocket(dataFd, string_view_t(host));
		tlsSocket.Handshake();
		return EitherSocket(std::move(tlsSocket));
	}

	longlong_t FtpClient::ReadEither(EitherSocket& _socket, const std::span<std::byte> _buffer)
	{
		return std::visit([&](auto& _s) { return _s.Read(_buffer); }, _socket);
	}

	void_t FtpClient::WriteEither(EitherSocket& _socket, const std::span<const std::byte> _data)
	{
		std::visit([&](auto& _s) { _s.Write(_data); }, _socket);
	}

END_NS
