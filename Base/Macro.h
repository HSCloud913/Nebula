//
// Created by hscloud on 24. 5. 19.
//

#pragma once

// 컴파일러/ABI 매크로 — OS 헤더에 의존하지 않으므로 Base/Type.h 가 계속 include 합니다.
#if defined(_USRDLL)
#define NEBULA_API __declspec(dllexport)
#else
#define NEBULA_API
#endif

#ifndef NOT_BUILD_NEBULA_DEPRECATE
#	if defined(_MSC_VER)
#		define NOT_BUILD_NEBULA_DEPRECATE __declspec(deprecated)
#	else
#		define NOT_BUILD_NEBULA_DEPRECATE [[deprecated]]
#	endif
#endif


// 복사/이동 특수 멤버를 한 줄로 선언하는 보일러플레이트 매크로.
// @note NEBULA_NON_COPYABLE_MOVABLE 은 "복사 불가 + **이동도 불가**" 를 뜻한다(이름이 오해를 부르기
//       쉬운데, MOVABLE 은 NON_ 의 목적어다). 복사만 막고 이동은 허용하려면
//       NEBULA_NON_COPYABLE + NEBULA_DEFAULT_MOVE 를 함께 쓴다.

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

#define NEBULA_DEFAULT_COPY_MOVE(Class) \
NEBULA_DEFAULT_COPY(Class) \
NEBULA_DEFAULT_MOVE(Class)

#define NEBULA_NON_COPYABLE_MOVABLE(Class) \
NEBULA_NON_COPYABLE(Class) \
NEBULA_NON_MOVABLE(Class)
