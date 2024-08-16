//
// Created by hscloud on 24. 5. 19.
//

#ifndef NEBULA_TYPE_H
#define NEBULA_TYPE_H

#include <string>
#include <type_traits>

/* OS */
#if _WIN32
//define something for Windows (32-bit and 64-bit, this part is common)
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


#define BEGIN_NS(name) namespace name {
#define END_NS }

#if defined(_USRDLL)
#define NEBULA_API __declspec(dllexport)
#else
#define NEBULA_API
#endif

#ifndef NOT_BUILD_NEBULA_DEPRECATE
#define NOT_BUILD_NEBULA_DEPRECATE __declspec(deprecated)
#endif

#define NEBULA_DEFAULT_COPY(Class) \
Class(const Class &) = default;\
Class &operator=(const Class &) = default;

#define NEBULA_DEFAULT_MOVE(Class) \
Class(Class &&) noexcept = default; \
Class &operator=(Class &&) noexcept = default;

#define NEBULA_NON_COPYABLE(Class) \
Class(const Class &) = delete;\
Class &operator=(const Class &) = delete;

#define NEBULA_NON_MOVABLE(Class) \
Class(Class &&) noexcept = delete; \
Class &operator=(Class &&) noexcept = delete;

#define NEBULA_NON_COPYABLE_MOVABLE(Class) \
NEBULA_NON_COPYABLE(Class) \
NEBULA_NON_MOVABLE(Class)

BEGIN_NS(Ne)
	typedef std::string string_t;
	typedef std::string_view string_view_t;

	typedef std::wstring wstring_t;
	typedef std::wstring_view wstring_view_t;

	typedef char* lpstr_t;
	typedef const char* lpcstr_t;

	typedef wchar_t* lpwstr_t;
	typedef const wchar_t* lpcwstr_t;

	typedef char char_t;
	typedef unsigned char byte_t;

	typedef short short_t, int16_t;
	typedef unsigned short ushort_t, uint16_t, word_t;

	typedef int int_t;
	typedef unsigned int uint_t;

	typedef long long_t;
	typedef unsigned long ulong_t, dword_t;

	typedef long long longlong_t;
	typedef unsigned long long ulonglong_t, dwordlong_t;

	typedef float float_t;
	typedef double double_t;

	typedef bool bool_t;

	typedef void void_t;

END_NS

#endif //NEBULA_TYPE_H
