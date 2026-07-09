//
// Created by hscloud on 26. 7. 7.
//

#pragma once
#include <cstdint>
#include <span>
#include "Base/Result.h"
#include "Base/Error.h"
#include "Base/Type.h"
#include "Io/IoType.h"
#include "Io/IoError.h"

BEGIN_NS(ne::io)
	// 플랫폼별 의미가 다른 opaque 핸들 — RIO_BUFFERID(Win) / io_uring buf index(Linux).
	// value == 0 은 미등록/무효. (RIO 는 실패 시 RIO_INVALID_BUFFERID 를 0 으로 정규화해 저장)
	struct BufferHandle
	{
		uint64_t value{ 0 };

		[[nodiscard]] bool_t IsValid() const noexcept { return value != 0; }
	};

	// 등록 버퍼 provider — RIO(IocpEngine)/io_uring Fixed Buffer(IoUringEngine) 처럼 사전 등록이
	// 필요한 엔진만 구현한다. 접근 경로: IEngine::AsRegisteredBufferProvider() (미지원 엔진은
	// nullptr 반환). 파일 I/O 를 IEngine 순수가상에 넣지 않는 것과 같은 이유 — 보편 지원 불가
	// 기능은 별도 인터페이스로 분리하고 capability 로 discover 한다.
	class IRegisteredBufferProvider
	{
	public:
		IRegisteredBufferProvider() = default;
		virtual ~IRegisteredBufferProvider() = default;

		NEBULA_NON_COPYABLE_MOVABLE(IRegisteredBufferProvider)

	public:
		// _region 전체를 커널/드라이버에 등록하고 핸들을 반환한다. 이후 이 영역 내부를 가리키는
		// 포인터로 SubmitSendRegistered/SubmitReceiveRegistered 를 호출한다(provider 가 등록 시점의
		// base 로부터 오프셋을 내부에서 계산 — 호출자는 별도 오프셋을 넘기지 않는다).
		// 등록/해제는 비싼 연산이므로 호출자는 버퍼를 풀링해 재사용하는 것을 권장한다.
		// 수명 불변식: 이 영역은 I/O 완료 전까지, 그리고 UnregisterBuffer 전까지 주소가 바뀌면 안 된다
		// — 호출자가 그 수명을 보장한다.
		[[nodiscard]] virtual ne::Result<BufferHandle, IoError> RegisterBuffer(std::span<ne::byte_t> _region) noexcept = 0;
		virtual void_t UnregisterBuffer(BufferHandle _handle) noexcept = 0;

	public:
		// 등록 버퍼 송수신 제출(fast path). _buffer 는 RegisterBuffer(_region) 에 넘겼던 그 영역
		// 내부의 포인터여야 한다(sub-range 허용). _userData 는 다른 op 들과 동일한 Completion.userData
		// 계약을 그대로 따른다 — 완료는 엔진의 WaitCompletions() 가 같은 Completion{userData, result}
		// 형태로 돌려준다(RIO: 엔진이 RIO_CQ 를 자신의 IOCP 에 바인딩해 정규화). 에러 타입은
		// File/Socket 과 동일한 IoError 로 통일한다(OS 실패는 IoErrorKind::OS_FAILURE 로 감싼다).
		[[nodiscard]] virtual ne::Result<void_t, IoError> SubmitSendRegistered(socket_t _socket, BufferHandle _handle, const void_t* _buffer, std::size_t _length, void_t* _userData) noexcept = 0;
		[[nodiscard]] virtual ne::Result<void_t, IoError> SubmitReceiveRegistered(socket_t _socket, BufferHandle _handle, void_t* _buffer, std::size_t _length, void_t* _userData) noexcept = 0;

	public:
		// 소켓 close 시 그 소켓에 묶인 provider-내부 상태를 정리한다(RIO: 소켓별 RIO_RQ 맵 엔트리).
		// 기본 no-op — 소켓별 상태가 없는 provider(예: io_uring)는 재정의하지 않아도 된다.
		virtual void_t ReleaseSocket(socket_t _socket) noexcept {}
	};
END_NS
