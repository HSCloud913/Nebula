//
// Created by nebula on 24. 5. 29.
//

#include "UdpSocket.h"

#include <thread>
#if defined(IS_POSIX)
#	include <fcntl.h>
#endif
#include "StringFormat.h"

/* Largest possible UDP/IPv4 payload (65535 - 8 byte UDP header - 20 byte IP header). A receive
 * buffer smaller than the datagram truncates it (and errors with WSAEMSGSIZE on Windows), so the
 * internal event-driven read path always uses a buffer this size. */
namespace
{
	constexpr std::size_t MaxDatagramSize = 65507;
}

BEGIN_NS(ne::protocol)
	UdpSocket::UdpSocket(const string_view_t _server, const int_t _port)
		: addressInfo(ResolveAddress(_server, _port))
		, handle(CreateHandle())
	{
	}



	void_t UdpSocket::Bind()
	{
		if (::bind(handle.Get(), addressInfo->ai_addr, static_cast<socklen_t>(addressInfo->ai_addrlen)) == -1)
		{
			throw ne::Exception("[UdpSocket/Bind]", std::format("Failed to bind socket (result: {})", GetSocketError()));
		}
	}

	void_t UdpSocket::Connect()
	{
		if (::connect(handle.Get(), addressInfo->ai_addr, static_cast<int_t>(addressInfo->ai_addrlen)) == -1)
		{
			throw ne::Exception("[UdpSocket/Connect]", std::format("Failed to connect socket (error: {})", GetSocketError()));
		}

		isConnected = true;
	}



	void_t UdpSocket::Select()
	{
		fd_set readFds, exceptFds;
		FD_ZERO(&readFds);
		FD_ZERO(&exceptFds);

		FD_SET(handle.Get(), &readFds);
		FD_SET(handle.Get(), &exceptFds);

		if (::select(0, &readFds, nullptr, &exceptFds, nullptr) < 0)
		{
			throw ne::Exception("[UdpSocket/Select]", std::format("Failed to select socket (result: {})", GetSocketError()));
		}

		if (FD_ISSET(handle.Get(), &readFds)) ProcessReadEvent();
		if (FD_ISSET(handle.Get(), &exceptFds)) ProcessErrorEvent();
	}

	void_t UdpSocket::Poll()
	{
		pollfd pollFds[1];
		pollFds[0].fd = handle.Get();
		pollFds[0].events = POLLIN | POLLERR;

#if defined(_WIN32)
		if (::WSAPoll(pollFds, 1, 0) < 0)
		{
			throw ne::Exception("[UdpSocket/Poll]", std::format("Failed to poll socket (result: {})", GetSocketError()));
		}
#elif defined(IS_POSIX)
		if (::poll(pollFds, 1, 0) < 0)
		{
			throw ne::Exception("[UdpSocket/Poll]", std::format("Failed to poll socket (result: {})", GetSocketError()));
		}
#endif

		if (pollFds[0].revents & POLLIN) ProcessReadEvent();
		if (pollFds[0].revents & POLLERR) ProcessErrorEvent();
	}



	void_t UdpSocket::SetSocketOption(int _option, const char* _value, int _valueLength) const
	{
		if (::setsockopt(handle.Get(), SOL_SOCKET, _option, _value, _valueLength) == -1)
		{
			throw ne::Exception("[UdpSocket/SetSocketOption]", std::format("Failed to setsockopt function | %d (error: {})", _option, GetSocketError()));
		}
	}

	void_t UdpSocket::SocketControl(long_t _option, ulong_t _value, bool _isEnable) const
	{
#if defined(_WIN32)
		if (::ioctlsocket(handle.Get(), _option, &_value) == -1)
		{
			throw ne::Exception("[UdpSocket/SocketControl]", std::format("Failed to ioctlsocket function | %d (error: {})", _option, GetSocketError()));
		}
#elif defined(IS_POSIX)
		const auto flags = ::fcntl(handle.Get(), F_GETFL);
		if (_isEnable)
		{
			if (::fcntl(handle.Get(), _option, flags | _value) == -1)
			{
				throw ne::Exception("[UdpSocket/SocketControl]", std::format("Failed to fcntl function | %d (error: {})", _option, GetSocketError()));
			}
		}
		else
		{
			if (::fcntl(handle.Get(), _option, flags & ~_value) == -1)
			{
				throw ne::Exception("[UdpSocket/SocketControl]", std::format("Failed to fcntl function | %d (error: {})", _option, GetSocketError()));
			}
		}
#endif
	}

	void_t UdpSocket::SetSocketMode(bool _isNonblocking)
	{
		if (isNonblocking == _isNonblocking) return;
#if defined(_WIN32)
		SocketControl(FIONBIO, _isNonblocking ? 1 : 0);
#elif defined(IS_POSIX)
		SocketControl(F_SETFL, O_NONBLOCK, _isNonblocking);
#endif
		isNonblocking = _isNonblocking;
	}



	int_t UdpSocket::Read(const std::span<std::byte> _buffer)
	{
		if (!isConnected) return -1;

		if (const auto result = ::recv(handle.Get(), reinterpret_cast<char_t*>(_buffer.data()), static_cast<int_t>(_buffer.size()), 0); result >= 0)
		{
			return static_cast<int_t>(result);
		}

		const auto errorCode = GetSocketError();
#if defined(_WIN32)
		if (isNonblocking && errorCode == WSAEWOULDBLOCK) return -1;
#elif defined(IS_POSIX)
		if (isNonblocking && (errorCode == EWOULDBLOCK || errorCode == EAGAIN)) return -1;
#endif

		throw ne::Exception("[UdpSocket/Read]", std::format("Failed to receive data through socket (error: {})", errorCode));
	}

	bool_t UdpSocket::Write(const std::span<const std::byte> _data)
	{
		if (!isConnected) return false;

		if (::send(handle.Get(), reinterpret_cast<const char_t*>(_data.data()), static_cast<int_t>(_data.size()), 0) == -1)
		{
			throw ne::Exception("[UdpSocket/Write]", std::format("Failed to send data through socket (error: {})", GetSocketError()));
		}

		return true;
	}

	int_t UdpSocket::ReadFrom(const std::span<std::byte> _buffer, sockaddr* _fromAddress, socklen_t* _fromAddressLength)
	{
		if (const auto result = ::recvfrom(handle.Get(), reinterpret_cast<char_t*>(_buffer.data()), static_cast<int_t>(_buffer.size()), 0, _fromAddress, _fromAddressLength); result >= 0)
		{
			return static_cast<int_t>(result);
		}

		const auto errorCode = GetSocketError();
#if defined(_WIN32)
		if (isNonblocking && errorCode == WSAEWOULDBLOCK) return -1;
#elif defined(IS_POSIX)
		if (isNonblocking && (errorCode == EWOULDBLOCK || errorCode == EAGAIN)) return -1;
#endif

		throw ne::Exception("[UdpSocket/ReadFrom]", std::format("Failed to receive data through socket (error: {})", errorCode));
	}

	bool_t UdpSocket::WriteTo(const std::span<const std::byte> _data, const sockaddr* _toAddress, const socklen_t _toAddressLength)
	{
		if (::sendto(handle.Get(), reinterpret_cast<const char_t*>(_data.data()), static_cast<int_t>(_data.size()), 0, _toAddress, _toAddressLength) == -1)
		{
			throw ne::Exception("[UdpSocket/WriteTo]", std::format("Failed to send data through socket (error: {})", GetSocketError()));
		}

		return true;
	}



	UdpSocket::AddressInfo UdpSocket::ResolveAddress(const string_view_t _server, const int_t _port)
	{
#if defined(_WIN32)
		const auto server = StringFormat::UTF8toWCS(string_t(_server).c_str());
		const auto port = std::to_wstring(_port);
		constexpr auto hints = addrinfoW
		{
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_DGRAM,
			.ai_protocol = IPPROTO_UDP,
		};
		auto addressInfo = static_cast<addrinfoW*>(nullptr);

		if (const auto result = GetAddrInfoW(server.data(), port.data(), &hints, &addressInfo))
		{
			throw ne::Exception("[UdpSocket/ResolveAddress]", std::format("Failed to get address info for socket creation (result: {})", result));
		}
#elif defined(IS_POSIX)
		const auto port = std::to_string(_port);
		constexpr auto hints = addrinfo{
			.ai_flags{},
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_DGRAM,
			.ai_protocol = IPPROTO_UDP,
			.ai_addrlen{},
			.ai_addr{},
			.ai_canonname{},
			.ai_next{}
		};
		auto addressInfo = static_cast<addrinfo*>(nullptr);

		if (const auto result = ::getaddrinfo(_server.data(), port.data(), &hints, &addressInfo))
		{
			throw ne::Exception("[UdpSocket/ResolveAddress]", std::format("Failed to get address info for socket creation (result: {})", result));
		}
#endif

		return AddressInfo(addressInfo);
	}

	SocketHandle UdpSocket::CreateHandle() const
	{
		using namespace std::chrono_literals;

		auto handle = SocketHandle{};
		while ((handle = socket(addressInfo->ai_family, addressInfo->ai_socktype, addressInfo->ai_protocol)).Get() == INVALID_SOCKET)
		{
#if defined(_WIN32)
			constexpr int_t Inprogress = WSAEINPROGRESS;
#elif defined(IS_POSIX)
			constexpr int_t Inprogress = EINPROGRESS;
#endif
			if (const auto error = GetSocketError(); error != Inprogress)
			{
				throw ne::Exception("[UdpSocket/CreateHandle]", std::format("Failed to create socket (error: {})", error));
			}
			std::this_thread::sleep_for(100ms);
		}

		return handle;
	}



	void_t UdpSocket::ProcessReadEvent()
	{
		try
		{
			auto data = std::vector<std::byte>(MaxDatagramSize);
			if (const auto readSize = Read(data); readSize >= 0)
			{
				data.resize(readSize);
				if (readHandler) readHandler(data);
			}
		} catch (const ne::Exception& e)
		{
			if (exceptionHandler) exceptionHandler(e.what());
		}
	}

	void_t UdpSocket::ProcessErrorEvent()
	{
		int_t error = 0;
		int_t errorLength = sizeof(error);
		if (getsockopt(handle.Get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &errorLength) != 0)
		{
			if (exceptionHandler) exceptionHandler(std::format("[UdpSocket/ProcessErrorEvent] Exception socket (result: {})", error));
		}
		else
		{
			if (exceptionHandler) exceptionHandler(std::format("[UdpSocket/ProcessErrorEvent] Failed to getsockopt function (result: {})", GetSocketError()));
		}
	}

END_NS
