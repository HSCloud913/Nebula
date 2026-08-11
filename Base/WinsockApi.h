//
// Created by hscloud on 26. 8. 11.
//

#pragma once
#include "Base/WindowsApi.h" // WIN32_LEAN_AND_MEAN/NOMINMAX 가드를 확정한 뒤 windows.h 를 먼저 들인다

// Winsock(<winsock2.h> 계열)을 들여오는 **유일한 창구**입니다. 소켓 API 를 쓰는 파일은 이 헤더만
// include 하세요.
//
// @note <winsock2.h> 를 직접 include 하면 그것이 스스로 windows.h 를 끌어옵니다. 이때 NOMINMAX 가
//       아직 정의되지 않았으므로 max/min 매크로가 누출되고(MSVC 에서 실측), 이후 <algorithm> 을 쓰는
//       전혀 무관한 파일이 std::max 텍스트 치환으로 깨집니다. WindowsApi.h 를 앞세워 가드를 먼저
//       세우는 것이 이 헤더의 존재 이유입니다.
// @note WIN32_LEAN_AND_MEAN 덕분에 windows.h 가 레거시 winsock.h 를 끌어오지 않으므로, windows.h →
//       winsock2.h 순서에도 재정의 충돌이 없습니다.

#if defined(_WIN32)
#	include <winsock2.h>
#	include <ws2tcpip.h> // getaddrinfo/inet_pton 등 IPv6 대응 API
#	include <mswsock.h>  // AcceptEx/ConnectEx/RIO 등 Microsoft 확장
#endif
