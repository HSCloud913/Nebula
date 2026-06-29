//
// Created by nebula on 24. 6. 20.
//

#ifndef SOCKETBASE_H
#define SOCKETBASE_H

#include "Type.h"
#include "NebulaHandle.h"
#include "Exception.h"
#if defined(_WIN32)
#	include <winsock2.h>
#elif defined(IS_POSIX)
#	include <sys/socket.h>
#endif

BEGIN_NS(ne::protocol)
#if defined(_WIN32)
	typedef SOCKET socket_t;

	using SocketHandle = ne::Handle<socket_t, decltype([](const auto _socket)
	{
		if (_socket == INVALID_SOCKET) return;

		if (::shutdown(_socket, SD_BOTH) == SOCKET_ERROR)
		{
			if (const auto result = WSAGetLastError(); result != WSAENOTCONN)
			{
				throw ne::Exception("[SocketHandle]", std::format("Failed to shutdown socket connection (error: {})", result));
			}
		}
		closesocket(_socket);
	}), INVALID_SOCKET>;
#elif defined(IS_POSIX)
	typedef int_t socket_t;

	using SocketHandle = ne::Handle<int_t, decltype([](const auto _socket)
	{
		if (_socket == -1) return;

		if (::shutdown(_socket, SHUT_RDWR) == -1)
		{
			if (const auto result = errno; result != ENOTCONN)
			{
				throw ne::Exception("[SocketHandle]", std::format("Failed to shutdown socket connection (error: {})", result));
			}
		}
		close(_socket);
	}), -1>;
#endif

	inline int_t GetSocketError()
	{
#if defined(_WIN32)
		return WSAGetLastError();
#elif defined(IS_POSIX)
		return errno;
#endif
	}

END_NS

#endif //SOCKETBASE_H
