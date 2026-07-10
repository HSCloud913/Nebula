//
// Created by hscloud on 26. 7. 10.
//
// Level 0 — 플랫폼 기본 엔진 생성 지점. 상위(Level 1 이상)는 어떤 백엔드가 도는지 몰라야 하므로
// (스펙 2.1) 구체 엔진 타입을 아는 코드를 여기 하나로 모은다. 완료형(IOCP/io_uring)을 우선
// 시도하고, 그 인스턴스 확보가 실패하면(IsValid()==false — 커널/권한 문제 등) 리액터
// (WsaPoll/Epoll)로 조용히 폴백한다. 반환된 IEngine 의 실제 종류는 Supports() 로만 질의한다.

#pragma once
#include <memory>
#include "Io/Engine/IEngine.h"

BEGIN_NS(ne::io)
	// 이 플랫폼의 기본 엔진을 생성한다. 완료형 확보 실패 시 리액터로 폴백하며, 어느 경로든
	// IsValid() 가 true 인 엔진을 반환하는 것을 목표로 한다(리액터도 실패하면 그 무효 엔진을
	// 그대로 반환 — 호출자가 IsValid() 로 확인). 지원하지 않는 플랫폼에서는 nullptr.
	[[nodiscard]] std::unique_ptr<IEngine> MakeDefaultEngine();

END_NS
