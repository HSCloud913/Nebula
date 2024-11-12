//
// Created by hsclo on 24. 5. 29.
//

#include "TcpSocket.h"

#include <thread>
#if defined(IS_POSIX)
#	include <fcntl.h>
#endif
#include "StringFormat.h"



BEGIN_NS(ne::protocol)
	TcpSocket::TcpSocket(const string_view_t _server, const int_t _port)
		: addressInfo(GetAddressInfo(_server, _port))
		, handle(CreateHandle())
	{
	}

	TcpSocket::TcpSocket(const socket_t _socket)
		: addressInfo(nullptr)
		, handle(_socket)
	{
	}



	void_t TcpSocket::Connect()
	{
		using namespace std::chrono_literals;

		while (connect(handle.Get(), addressInfo->ai_addr, static_cast<int_t>(addressInfo->ai_addrlen)) == -1)
		{
#if defined(_WIN32)
			constexpr int_t Inprogress = WSAEINPROGRESS;
#elif defined(IS_POSIX)
			constexpr int_t Inprogress = EINPROGRESS;
#endif
			if (const auto error = GetSocketError(); error != Inprogress)
			{
				throw NebulaException("[TcpSocket/Connect]", std::format("Failed to connect socket (error: {})", error));
			}
			std::this_thread::sleep_for(1ms);
		}
	}

	void_t TcpSocket::Reconnect()
	{
		handle = CreateHandle();
		Connect();
	}

	bool_t TcpSocket::IsConnected() const
	{
#if defined(_WIN32)
		return handle.Get() != INVALID_SOCKET;
#elif defined(IS_POSIX)
		return handle.Get() != -1;
#endif
	}

	void_t TcpSocket::Bind()
	{
		if (::bind(handle.Get(), addressInfo->ai_addr, static_cast<socklen_t>(addressInfo->ai_addrlen)) == -1)
		{
			throw NebulaException("[TcpSocket/Bind]", std::format("Failed to bind socket (result: {})", GetSocketError()));
		}
	}

	void_t TcpSocket::Listen()
	{
		if (::listen(handle.Get(), SOMAXCONN) == -1)
		{
			throw NebulaException("[TcpSocket/Listen]", std::format("Failed to listen socket (result: {})", GetSocketError()));
		}
	}

	socket_t TcpSocket::Accept(sockaddr* _sockAddr, socklen_t* _addrLength)
	{
		auto socket = ::accept(handle.Get(), _sockAddr, _addrLength);
#if defined(_WIN32)
		if (socket == INVALID_SOCKET)
		{
			throw NebulaException("[TcpSocket/Accept]", std::format("Failed to accept socket (result: {})", GetSocketError()));
		}
#elif defined(IS_POSIX)
		if (socket == -1)
		{
			throw NebulaException("[TcpSocket/Accept]", std::format("Failed to accept socket (result: {})", GetSocketError()));
		}
#endif

		return socket;
	}


	void_t TcpSocket::Select()
	{
		fd_set readFds, writeFds, exceptFds;

		FD_ZERO(&readFds);
		FD_ZERO(&writeFds);
		FD_ZERO(&exceptFds);

		FD_SET(handle.Get(), &readFds);
		FD_SET(handle.Get(), &writeFds);
		FD_SET(handle.Get(), &exceptFds);

		if (::select(0, &readFds, &writeFds, &exceptFds, nullptr) < 0)
		{
			throw NebulaException("[TcpSocket/Select]", std::format("Failed to select socket (result: {})", GetSocketError()));
		}

		if (FD_ISSET(handle.Get(), &readFds))
		{
		}

		if (FD_ISSET(handle.Get(), &writeFds))
		{
		}

		if (FD_ISSET(handle.Get(), &exceptFds))
		{
		}
	}

	void_t TcpSocket::Poll()
	{
		pollfd pollFds[1];
		pollFds[0].fd = handle.Get();
		pollFds[0].events = POLLIN | POLLOUT | POLLERR;

#if defined(_WIN32)
		if (::WSAPoll(pollFds, 1, 0) < 0)
		{
			throw NebulaException("[TcpSocket/Poll]", std::format("Failed to poll socket (result: {})", GetSocketError()));
		}
#elif defined(IS_POSIX)
		if (::poll(pollFds, 1, 0) < 0)
		{
			throw NebulaException("[TcpSocket/Poll]", std::format("Failed to poll socket (result: {})", GetSocketError()));
		}
#endif

		if (pollFds[0].revents & POLLIN)
		{
		}

		if (pollFds[0].revents & POLLOUT)
		{
		}

		if (pollFds[0].revents & POLLERR)
		{
		}
	}
#if defined(__linux)
	void_t TcpSocket::Epoll()
	{
	}
#elif defined(__APPLE__)
	void_t TcpSocket::Kqueue()
	{
	}
#endif



	void_t TcpSocket::SetSocketOption(int _option, const char* _value, int _valueLength) const
	{
		if (::setsockopt(handle.Get(), SOL_SOCKET, _option, _value, _valueLength) == -1)
		{
			throw NebulaException("[TcpSocket/SetSocketOption]", std::format("Failed to setsockopt function | %d (error: {})", _option, GetSocketError()));
		}
	}

	void_t TcpSocket::SocketControl(long_t _option, ulong_t _value, bool _isEnable) const
	{
#if defined(_WIN32)
		if (::ioctlsocket(handle.Get(), _option, &_value) == -1)
		{
			throw NebulaException("[TcpSocket/SetSocketControl]", std::format("Failed to ioctlsocket function | %d (error: {})", _option, GetSocketError()));
		}
#elif defined(IS_POSIX)
		const auto flags = ::fcntl(handle.Get(), F_GETFL);
		if (_isEnable)
		{
			if (::fcntl(handle.Get(), _option, flags | _value) == -1)
			{
				throw NebulaException("[TcpSocket/SetSocketControl]", std::format("Failed to fcntl function | %d (error: {})", _option, GetSocketError()));
			}
		}
		else
		{
			if (::fcntl(handle.Get(), _option, flags & ~_value) == -1)
			{
				throw NebulaException("[TcpSocket/SetSocketControl]", std::format("Failed to fcntl function | %d (error: {})", _option, GetSocketError()));
			}
		}
#endif
	}


	void_t TcpSocket::SetNonBlockingMode(bool_t _isNonblocking)
	{
		if (isNonblocking == _isNonblocking) return;
#if defined(_WIN32)
		SocketControl(FIONBIO, (_isNonblocking) ? 1 : 0);
#elif defined(IS_POSIX)
		SocketControl(F_SETFL, O_NONBLOCK, _isNonblocking);
#endif

		isNonblocking = _isNonblocking;
	}



	std::size_t TcpSocket::Read(const std::span<std::byte> _buffer)
	{
		if (!IsConnected()) throw NebulaException("[TcpSocket/Read]", std::format("TcpSocket is not connected"));

		if (const auto result = recv(handle.Get(), reinterpret_cast<char_t*>(_buffer.data()), static_cast<int_t>(_buffer.size()), 0); result > 0)
		{
			return result;
		}
		else if (result == 0)
		{
			handle = SocketHandle{};
			return 0;
		}

		auto errorCode = GetSocketError();
#if defined(_WIN32)
		if (isNonblocking && errorCode == WSAEWOULDBLOCK) return {};
#elif defined(IS_POSIX)
		if (isNonblocking && (errorCode == EWOULDBLOCK || error == EAGAIN)) return {};
#endif
		throw NebulaException("[TcpSocket/Read]", std::format("Failed to receive data through socket (error: {})", errorCode));
	}

	void_t TcpSocket::Write(const std::span<const std::byte> _data)
	{
		if (!IsConnected()) throw NebulaException("[TcpSocket/Write]", std::format("TcpSocket is not connected"));

		if (::send(handle.Get(), reinterpret_cast<const char_t*>(_data.data()), static_cast<int_t>(_data.size()), 0) == -1)
		{
			throw NebulaException("[TcpSocket/Write]", std::format("Failed to send data through socket (error: {})", GetSocketError()));
		}
	}



	SocketHandle TcpSocket::CreateHandle() const
	{
#if defined(_WIN32)
		using namespace std::chrono_literals;

		auto handle = SocketHandle{};
		while ((handle = socket(addressInfo->ai_family, addressInfo->ai_socktype, addressInfo->ai_protocol)).Get() == INVALID_SOCKET)
		{
			if (const auto error = GetSocketError(); error != WSAEINPROGRESS)
			{
				throw NebulaException("[TcpSocket/CreateHandle]", std::format("Failed to create socket (error: {})", error));
			}
			std::this_thread::sleep_for(1ms);
		}
#elif defined(IS_POSIX)
		auto handle = SocketHandle{};
		while ((handle = socket(addressInfo->ai_family, addressInfo->ai_socktype, addressInfo->ai_protocol)).Get() == -1)
		{
			if (const auto error = GetSocketError(); error != EINPROGRESS)
			{
				throw NebulaException("[TcpSocket/CreateHandle]", std::format("Failed to create socket (error: {})", error));
			}
			std::this_thread::sleep_for(1ms);
		}
#endif

		return handle;
	}

	TcpSocket::AddressInfo TcpSocket::GetAddressInfo(const string_view_t _server, const int_t _port)
	{
#if defined(_WIN32)
		const auto server = StringFormat::UTF8toWCS(string_t(_server).c_str());
		const auto port = std::to_wstring(_port);
		constexpr auto hints = addrinfoW
		{
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_STREAM,
			.ai_protocol = IPPROTO_TCP,
		};
		auto addressInfo = static_cast<addrinfoW*>(nullptr);

		if (const auto result = GetAddrInfoW(server.data(), port.data(), &hints, &addressInfo))
		{
			throw NebulaException("[TcpSocket/GetAddressInfo]", std::format("Failed to get address info for socket creation (result: {})", result));
		}
#elif defined(IS_POSIX)
		const auto port = std::to_string(_port);
		constexpr auto hints = addrinfo{
			.ai_flags{},
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_STREAM,
			.ai_protocol = IPPROTO_TCP,
			.ai_addrlen{},
			.ai_addr{},
			.ai_canonname{},
			.ai_next{}
		};
		auto addressInfo = static_cast<addrinfo*>(nullptr);

		if (const auto result = ::getaddrinfo(_server.data(), port.data(), &hints, &addressInfo))
		{
			throw NebulaException("[TcpSocket/GetAddressInfo]", std::format("Failed to get address info for socket creation (result: {})", result));
		}
#endif

		return AddressInfo(addressInfo);
	}



	socket_t TcpSocket::GetHandle() const
	{
		return handle.Get();
	}

END_NS
