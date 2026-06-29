//
// Created by hscloud on 25. 6. 29.
//

#include "Socket.h"
#include <cstring>
#include <cerrno>

#if defined(_WIN32)
#   include <winsock2.h>
#   include <ws2tcpip.h>
#elif defined(IS_POSIX)
#   include <netinet/tcp.h>
#endif



BEGIN_NS(ne::network)
	Socket::Socket(const socket_t _fd)
		: handle(_fd) {}



	ne::Result<Socket, ne::OsError> Socket::CreateTcp()
	{
		const socket_t fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (fd == InvalidSocket)
			return ne::Result<Socket, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/CreateTcp]")
			);

		return ne::Result<Socket, ne::OsError>::Ok(Socket{ fd });
	}

	ne::Result<Socket, ne::OsError> Socket::CreateUdp()
	{
		const socket_t fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (fd == InvalidSocket)
			return ne::Result<Socket, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/CreateUdp]")
			);

		return ne::Result<Socket, ne::OsError>::Ok(Socket{ fd });
	}



	ne::Result<void, ne::OsError> Socket::SetReuseAddr(const bool_t _enable)
	{
		const int_t val = _enable ? 1 : 0;
		if (::setsockopt(handle.Get(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char_t*>(&val), sizeof(val)) != 0)
		{
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/SetReuseAddr]")
			);
		}

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> Socket::SetNoDelay(bool_t _enable)
	{
		const int_t val = _enable ? 1 : 0;
		if (::setsockopt(handle.Get(), IPPROTO_TCP, TCP_NODELAY,
						reinterpret_cast<const char_t*>(&val), sizeof(val)) != 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/SetNoDelay]")
			);
		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> Socket::SetNonBlocking(bool_t _enable)
	{
#if defined(_WIN32)
		u_long mode = _enable ? 1u : 0u;
		if (::ioctlsocket(handle.Get(), FIONBIO, &mode) != 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/SetNonBlocking]")
			);
#elif defined(IS_POSIX)
		int_t flags = ::fcntl(handle.Get(), F_GETFL, 0);
		if (flags == -1)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ errno }.Context("[Socket/SetNonBlocking]")
			);

		flags = _enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
		if (::fcntl(handle.Get(), F_SETFL, flags) == -1)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ errno }.Context("[Socket/SetNonBlocking]")
			);
#endif

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> Socket::SetRecvTimeout(std::chrono::milliseconds _timeout)
	{
#if defined(_WIN32)
		const DWORD ms = static_cast<DWORD>(_timeout.count());
		if (::setsockopt(handle.Get(), SOL_SOCKET, SO_RCVTIMEO,
						reinterpret_cast<const char_t*>(&ms), sizeof(ms)) != 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/SetRecvTimeout]")
			);
#elif defined(IS_POSIX)
		const timeval tv{
			.tv_sec = static_cast<time_t>(_timeout.count() / 1000),
			.tv_usec = static_cast<suseconds_t>((_timeout.count() % 1000) * 1000)
		};
		if (::setsockopt(handle.Get(), SOL_SOCKET, SO_RCVTIMEO,
						reinterpret_cast<const char_t*>(&tv), sizeof(tv)) != 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ errno }.Context("[Socket/SetRecvTimeout]")
			);
#endif
		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> Socket::SetSendTimeout(std::chrono::milliseconds _timeout)
	{
#if defined(_WIN32)
		const DWORD ms = static_cast<DWORD>(_timeout.count());
		if (::setsockopt(handle.Get(), SOL_SOCKET, SO_SNDTIMEO,
						reinterpret_cast<const char_t*>(&ms), sizeof(ms)) != 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/SetSendTimeout]")
			);
#elif defined(IS_POSIX)
		const timeval tv{
			.tv_sec = static_cast<time_t>(_timeout.count() / 1000),
			.tv_usec = static_cast<suseconds_t>((_timeout.count() % 1000) * 1000)
		};
		if (::setsockopt(handle.Get(), SOL_SOCKET, SO_SNDTIMEO,
						reinterpret_cast<const char_t*>(&tv), sizeof(tv)) != 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ errno }.Context("[Socket/SetSendTimeout]")
			);
#endif
		return ne::Result<void, ne::OsError>::Ok();
	}



	ne::Result<void, ne::OsError> Socket::Bind(string_view_t _address, uint16_t _port)
	{
		auto resolved = ResolveAddress(_address, _port);
		if (!resolved)
		{
			resolved.Error().Context("[Socket/Bind]");
			return ne::Result<void, ne::OsError>::Error(std::move(resolved.Error()));
		}

		if (::bind(handle.Get(),
					reinterpret_cast<const sockaddr*>(&resolved.Value()),
					sizeof(sockaddr_in)) != 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/Bind]")
			);

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> Socket::Listen(int_t _backlog)
	{
		if (::listen(handle.Get(), _backlog) != 0)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/Listen]")
			);
		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<Socket, ne::OsError> Socket::Accept()
	{
		sockaddr_in addr{};
		socklen_t addrLen = sizeof(addr);

		const socket_t clientFd = ::accept(
			handle.Get(),
			reinterpret_cast<sockaddr*>(&addr),
			&addrLen
		);

		if (clientFd == InvalidSocket)
			return ne::Result<Socket, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/Accept]")
			);

		return ne::Result<Socket, ne::OsError>::Ok(Socket{ clientFd });
	}



	ne::Result<void, ne::OsError> Socket::Connect(string_view_t _address, uint16_t _port)
	{
		auto resolved = ResolveAddress(_address, _port);
		if (!resolved)
		{
			resolved.Error().Context("[Socket/Connect]");
			return ne::Result<void, ne::OsError>::Error(std::move(resolved.Error()));
		}

		if (::connect(handle.Get(),
					reinterpret_cast<const sockaddr*>(&resolved.Value()),
					sizeof(sockaddr_in)) != 0)
		{
			const ulong_t lastOsError = LastOsError();
#if defined(_WIN32)
			if (lastOsError != WSAEINPROGRESS && lastOsError != WSAEWOULDBLOCK)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ lastOsError }.Context("[Socket/Connect]")
				);
#elif defined(IS_POSIX)
			if (errno != EINPROGRESS)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ lastOsError }.Context("[Socket/Connect]")
				);
#endif
		}

		return ne::Result<void, ne::OsError>::Ok();
	}



	ne::Result<sockaddr_in, ne::OsError> Socket::ResolveAddress(
		string_view_t _address,
		uint16_t _port)
	{
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = ::htons(_port);

		const ne::string_t addrStr(_address);

		if (::inet_pton(AF_INET, addrStr.c_str(), &addr.sin_addr) == 1) return ne::Result<sockaddr_in, ne::OsError>::Ok(addr);

		// 호스트 이름 → IP 변환
		addrinfo hints{};
		addrinfo* result = nullptr;
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;

		if (::getaddrinfo(addrStr.c_str(), nullptr, &hints, &result) != 0)
			return ne::Result<sockaddr_in, ne::OsError>::Error(
				ne::OsError{ LastOsError() }.Context("[Socket/ResolveAddress]")
			);

		addr.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
		::freeaddrinfo(result);

		return ne::Result<sockaddr_in, ne::OsError>::Ok(addr);
	}

END_NS
