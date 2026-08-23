//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include "Base/Type.h"

#if defined(_WIN32)
#include "Base/WinsockApi.h"

namespace ne::io
{
	using file_t = HANDLE;
	using socket_t = SOCKET;
	using fd_t = socket_t;
	inline const auto InvalidFile = INVALID_HANDLE_VALUE;
	inline const auto InvalidSocket = INVALID_SOCKET;
}

#elif defined(IS_POSIX)
#   include <sys/socket.h>

namespace ne::io
{
	using file_t = int_t;
	using socket_t = int_t;
	using fd_t = socket_t;
	inline constexpr file_t InvalidFile = -1;
	inline constexpr socket_t InvalidSocket = -1;
}

#endif
