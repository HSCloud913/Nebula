//
// Created by hscloud on 24. 5. 19.
//

#pragma once
#include <string>
#include "Base/Platform.h"
#include "Base/Macro.h"

// 이 헤더는 프로젝트 공통 typedef 를 담고, 플랫폼 감지(Platform.h)와 매크로(Macro.h)를 함께
// 전파합니다 — 둘 다 OS 헤더를 include 하지 않아 비용이 없고, 특히 IS_POSIX 는 정의가 누락되면
// `#if defined(IS_POSIX)` 가 **에러 없이 조용히 거짓**이 되어 엉뚱한 분기가 컴파일되므로 의도적으로
// 어디서나 보이게 둡니다.
//
// **<windows.h> 는 여기서 들어오지 않습니다**(2026-08-11 분리). Windows API 타입/함수를 쓰는 파일만
// "Base/WindowsApi.h" 를 직접 include 하세요.

namespace ne
{
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
	typedef unsigned short ushort_t, uint16_t;

	typedef int int_t;
	typedef unsigned int uint_t;

	typedef long long_t;
	typedef unsigned long ulong_t;

	typedef long long longlong_t;
	typedef unsigned long long ulonglong_t;

	typedef float float_t;
	typedef double double_t;

	typedef bool bool_t;

	typedef void void_t;
}
