//
// Created by hscloud on 26. 7. 1.
//
// Level 0 — 엔진 공통 인터페이스. 완료 기반(proactor) 모양으로 통일한다(스펙 2.1, 8절).
// Reactor 계열(WsaPollEngine/EpollEngine)은 내부에서 readiness 알림을 받아 read/write 를
// 직접 호출해 synthetic completion 을 만들어 동일한 WaitCompletions 시그니처로 노출한다.
//
// 핵심 원칙: Level 1 이상은 어떤 엔진이 도는지 몰라야 한다. 엔진 종류/zero-copy 지원 여부는
// 오직 Supports(Capability) 로만 런타임 질의하며, 상위 코드에 엔진별 분기가 들어가면 설계 위반이다.
//
// 확장성: 지금 Windows 백엔드는 IocpEngine(IOCP) 하나지만, 이 계약(Submit/WaitCompletions/
// Wake/Supports)은 native 완료 큐에 그대로 대응하므로 추후 IoRing(Windows 11+) 백엔드를
// 같은 인터페이스로 추가할 수 있다(엔진 이름/타입에 수식어를 붙이지 않는다 — 스펙 2.1).

#pragma once
#include <chrono>
#include "Base/Type.h"
#include "Io/IoType.h"
#include "Io/Context/Operation.h"

BEGIN_NS(ne::io)
	class IEngine
	{
	public:
		NEBULA_NON_COPYABLE_MOVABLE(IEngine)

		IEngine() = default;
		virtual ~IEngine() = default;

	public:
		// 요청을 엔진에 제출한다(비동기). 완료는 WaitCompletions 로 회수된다.
		virtual void_t Submit(const Request& _request) = 0;

		// 완료를 최대 _max 개까지 _out 에 채우고 채운 개수를 반환한다. _timeout 동안 블록하며,
		// 완료가 없으면 0 을 반환한다(타임아웃). Reactor 계열은 readiness→completion 합성 결과를 함께 채운다.
		[[nodiscard]] virtual int_t WaitCompletions(Completion* _out, int_t _max, std::chrono::milliseconds _timeout) = 0;

		// 다른 스레드에서 WaitCompletions 블록을 깨운다(post 로 넘어온 작업 처리 등).
		virtual void_t Wake() = 0;

		// _userData 로 식별되는 진행 중 op 의 커널 레벨 취소를 요청한다(CancelIoEx / IORING_OP_ASYNC_CANCEL).
		// 취소는 요청일 뿐 — op 는 여전히 완료 통지(취소면 ERROR_OPERATION_ABORTED)되어 정상 경로로 회수된다.
		// 이미 완료됐거나 알 수 없는 userData 면 no-op. (스펙 4: 취소는 stop_token → 실제 커널 취소로 연결)
		virtual void_t Cancel(void_t* _userData) noexcept = 0;

		// 런타임 기능 질의 — 상위 API 가 *_fixed/*_zc 경로 사용 여부를 이걸로만 판단한다.
		[[nodiscard]] virtual bool_t Supports(Capability _capability) const = 0;

		// 엔진 생성이 성공했는지(예: IOCP/io_uring 인스턴스 확보) — 예외 없이 값으로 실패를
		// 드러내는 프로젝트 원칙에 따라, EngineFactory 가 프록시 생성 실패를 다형적으로
		// 확인하고 리액터로 폴백하기 위해 필요하다. 각 구현체가 이미 갖고 있던 비가상
		// IsValid() 를 여기로 승격한 것.
		[[nodiscard]] virtual bool_t IsValid() const noexcept = 0;

		// 등록 버퍼(zero-copy) provider 접근 — RIO(IocpEngine)/io_uring Fixed Buffer(IoUringEngine)
		// 처럼 사전 등록이 필요한 엔진만 재정의한다. 기본 nullptr(미지원 — EpollEngine/WsaPollEngine).
		// ReadFixed/WriteFixed/SendZeroCopy 는 provider 없이도 각 엔진이 폴백 동작을 제공할 수 있다
		// (자세한 매트릭스는 Supports() 참고) — 이건 어디까지나 "사전 등록"이 필요한 진짜 등록 경로용.
		[[nodiscard]] virtual class IRegisteredBufferProvider* AsRegisteredBufferProvider() noexcept { return nullptr; }
	};

END_NS
