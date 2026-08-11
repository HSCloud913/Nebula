//
// Created by hscloud on 26. 8. 11.
//

#pragma once
#include "Base/Platform.h"

// <windows.h> 를 안전한 매크로 가드와 함께 들여오는 **유일한 창구**입니다.
// Windows API 타입/함수(HANDLE, DWORD, GetLastError 등)를 쓰는 파일만 이 헤더를 include 하세요 —
// 과거에는 Base/Type.h 가 <windows.h> 를 끌어와 저장소 전체 번역 단위에 퍼졌습니다(2026-08-11 분리).
//
// @note NOMINMAX 가 없으면 windows.h 의 max/min 매크로가 std::max/std::min 호출을 텍스트 치환해,
//       전혀 무관한 파일에서 알아보기 힘든 구문 오류를 냅니다(과거 PoolAllocator/TimerQueue 에서 겪음).
//       가드를 각 파일에 흩뿌리지 않고 여기 한 곳에 모아 두는 것이 이 헤더의 존재 이유입니다.
// @note Winsock 을 쓰는 파일은 이 헤더가 아니라 "Base/WinsockApi.h" 를 include 하세요. <winsock2.h> 를
//       직접 include 하면 그것이 NOMINMAX 없이 windows.h 를 끌어와(MSVC 확인) 가드가 무력화됩니다.

#if defined(_WIN32)
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN // winsock.h 등 레거시 선언을 배제 — winsock2.h 와의 충돌 방지
#	endif
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif

#	include <windows.h>

// 위 가드보다 먼저 windows.h 가 들어왔다면 매크로가 이미 정의되어 있다 — 조용히 넘기면 엉뚱한 파일에서
// std::max/std::min 이 텍스트 치환되어 터지므로, 원인 지점에서 바로 실패시킨다.
#	ifdef max
#		error "windows.h 가 NOMINMAX 없이 먼저 들어왔습니다 — <windows.h>/<winsock2.h> 직접 include 를 Base/WindowsApi.h 또는 Base/WinsockApi.h 로 바꾸세요."
#	endif
#endif
