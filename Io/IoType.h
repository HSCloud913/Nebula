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
	// IIoEngine::Watch 로 감시 가능한 일반 디스크립터. POSIX 는 socket_t 와 동일(둘 다 fd)이지만
	// Windows 는 소켓과 파일/파이프 HANDLE 이 별개 타입이므로 별도로 구분해 둔다.
	using io_fd_t = socket_t;
	inline const auto InvalidFile = INVALID_HANDLE_VALUE;
	inline const auto InvalidSocket = INVALID_SOCKET;
END_NS

#elif defined(IS_POSIX)
#   include <sys/socket.h>

BEGIN_NS(ne::io)
	using file_t = int_t;
	using socket_t = int_t;
	// POSIX 에서는 소켓/파일/파이프 모두 int fd 이므로 socket_t 와 동일.
	using io_fd_t = socket_t;
	inline constexpr file_t InvalidFile = -1;
	inline constexpr socket_t InvalidSocket = -1;
END_NS

#endif
