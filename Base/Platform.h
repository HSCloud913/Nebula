//
// Created by hscloud on 24. 5. 19.
//

#pragma once

// 플랫폼 감지만 담당합니다 — **OS 헤더를 include 하지 않습니다.**
// Windows API 를 실제로 쓰는 곳은 "Base/WindowsApi.h" 를 직접 include 하세요(그래야 <windows.h> 가
// 저장소 전체로 전파되지 않습니다). 이 헤더는 Base/Type.h 가 계속 include 하므로 IS_POSIX 는
// 어디서나 정의되어 있습니다 — 정의가 누락되면 `#if defined(IS_POSIX)` 가 에러 없이 조용히 거짓이
// 되어 엉뚱한 분기가 컴파일되기 때문에, 이 부분은 의도적으로 전파합니다.

/* OS */
#if _WIN32
#	ifdef _WIN64
//define something for Windows (64-bit only)
#	endif
#elif __linux__
// linux
#elif __unix__
// Unix
#elif __APPLE__
#	include "TargetConditionals.h"
#	if TARGET_IPHONE_SIMULATOR
// iOS Simulator
#	elif TARGET_OS_IPHONE
// iOS device
#	elif TARGET_OS_MAC
// Other kinds of Mac OS
#	else
#		error "Unknown Apple platform"
#	endif
#elif defined(_POSIX_VERSION)
// POSIX
#else
#	error "Unknown compiler"
#endif

#ifdef _WIN32

#elif __has_include(<unistd.h>)
#define IS_POSIX
#endif
