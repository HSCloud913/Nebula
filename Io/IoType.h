//
// Created by csw on 26. 6. 30..
//

#pragma once
#include "Type.h"

#if defined(_WIN32)
	// windows.h 를 먼저 포함해야 winnt.h 가 필요로 하는 아키텍처 매크로(_AMD64_ 등)가 winsock
	// 계열 헤더보다 먼저 정의된다(그 반대 순서면 MSVC 에서 "No Target Architecture" C1189 로
	// 빌드가 깨진다). Type.h 가 이미 WIN32_LEAN_AND_MEAN 을 정의해 두어 windows.h 가 구식
	// winsock.h 를 끌어들이지 않으므로 winsock2.h 와 충돌하지 않는다.
#   include <windows.h>
#   include <winsock2.h>

BEGIN_NS(ne::io)
	using file_t = HANDLE;
	using socket_t = SOCKET;
	using fd_t = socket_t;
	inline const auto InvalidFile = INVALID_HANDLE_VALUE;
	inline const auto InvalidSocket = INVALID_SOCKET;
END_NS

#elif defined(IS_POSIX)
#   include <sys/socket.h>

BEGIN_NS(ne::io)
	using file_t = int_t;
	using socket_t = int_t;
	using fd_t = socket_t;
	inline constexpr file_t InvalidFile = -1;
	inline constexpr socket_t InvalidSocket = -1;
END_NS

#endif
