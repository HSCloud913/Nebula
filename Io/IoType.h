//
// Created by csw on 26. 6. 30..
//

#pragma once
#include "Type.h"

#if defined(_WIN32)
#   include <winsock2.h>
#   include <windows.h>

BEGIN_NS(ne::io)
	using file_t = HANDLE;
	using socket_t = SOCKET;
	inline const auto InvalidFile = INVALID_HANDLE_VALUE;
	inline const auto InvalidSocket = INVALID_SOCKET;
END_NS

#elif defined(IS_POSIX)
#   include <sys/socket.h>

BEGIN_NS(ne::io)
	using file_t = int_t;
	using socket_t = int_t;
	inline constexpr file_t InvalidFile = -1;
	inline constexpr socket_t InvalidSocket = -1;
END_NS

#endif
