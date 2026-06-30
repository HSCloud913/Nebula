//
// Created by csw on 26. 6. 30..
//

#pragma once

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
BEGIN_NS(ne::io)
	using file_t = HANDLE;
	inline const auto InvalidFile = INVALID_HANDLE_VALUE;
END_NS

#elif defined(IS_POSIX)
BEGIN_NS(ne::io)
	using file_t = int;
	inline constexpr file_t InvalidFile = -1;
END_NS

#endif
