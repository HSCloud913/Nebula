//
// Created by hscloud on 26. 7. 7.
//

#pragma once
#include <cstdint>
#include <span>
#include "Result.h"
#include "Error.h"
#include "Type.h"
#include "IoType.h"
#include "IoError.h"
#include "Buffer/BufferView.h"

BEGIN_NS(ne::io)
	struct ProactorContext; // 완료 컨텍스트(IIoEngine.h 정의). 제출 시 포인터만 넘기므로 전방선언으로 충분.
	// 플랫폼별 의미가 다른 opaque 핸들 — RIO_BUFFERID(Win) / io_uring buf index(Linux).
	// value == 0 은 미등록/무효. (RIO 는 실패 시 RIO_INVALID_BUFFERID 를 0 으로 정규화해 저장)
	struct BufferHandle
	{
		uint64_t value{ 0 };

		[[nodiscard]] bool_t IsValid() const noexcept { return value != 0; }
	};

	// 등록된 영역 안의 sub-view + 그 영역 핸들.
	//
	// 수명 불변식: view 가 가리키는 메모리는 I/O 완료 전까지 주소가 바뀌면 안 된다.
	// view.owner(BufferBlock) 를 refcount 로 pin 해 이를 보장한다 — IoContext::bufferOwner 와
	// 동일한 메커니즘이므로 별도 장치가 필요 없다. (RIO/io_uring 등록 버퍼의 핵심 제약)
	struct RegisteredBuffer
	{
		BufferHandle handle;
		BufferView view; // owner(BufferBlock) / ptr / length

		[[nodiscard]] bool_t IsValid() const noexcept { return handle.IsValid() && view.IsValid(); }
	};

	// 등록 버퍼 provider — IoCapability::RegisteredIo 를 가진 엔진만 구현한다.
	// 접근 경로: IIoEngine::AsRegisteredBufferProvider() (미지원 엔진은 nullptr 반환).
	// 파일 I/O 를 IIoEngine 순수가상에 넣지 않는 것과 같은 이유 — 보편 지원 불가 기능은
	// 별도 인터페이스로 분리하고 capability 로 discover 한다.
	class IRegisteredBufferProvider
	{
	public:
		IRegisteredBufferProvider() = default;
		virtual ~IRegisteredBufferProvider() = default;

		NEBULA_NON_COPYABLE_MOVABLE(IRegisteredBufferProvider)

	public:
		// _region 전체를 커널/드라이버에 등록하고 핸들을 반환한다. 이후 이 영역 내 임의의
		// sub-view 로 RegisteredBuffer 를 구성해 SendRegistered/ReceiveRegistered 에 넘긴다.
		// 등록/해제는 비싼 연산이므로 호출자는 버퍼를 풀링해 재사용하는 것을 권장한다.
		[[nodiscard]] virtual ne::Result<BufferHandle, IoError> RegisterBuffer(std::span<ne::byte_t> _region) noexcept = 0;
		virtual void UnregisterBuffer(BufferHandle _handle) noexcept = 0;

	public:
		// 등록 버퍼 송수신 제출(fast path). 완료는 엔진 이벤트 루프에서 _context 를 통해 통지된다
		// (RIO: DrainRioCompletions, io_uring: *_FIXED CQE). 에러 타입은 스트림 경로와 동일한 OsError
		// 로 통일한다(등록/미지원 계열 에러만 IoError 로 RegisterBuffer 에서 다룬다).
		[[nodiscard]] virtual ne::Result<void, ne::OsError> SubmitSendRegistered(socket_t _socket, const RegisteredBuffer& _buffer, ProactorContext* _context) noexcept = 0;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> SubmitReceiveRegistered(socket_t _socket, const RegisteredBuffer& _buffer, ProactorContext* _context) noexcept = 0;

	public:
		// 소켓 close 시 그 소켓에 묶인 provider-내부 상태를 정리한다(RIO: 소켓별 RIO_RQ 맵 엔트리).
		// 기본 no-op — 소켓별 상태가 없는 provider(예: io_uring)는 재정의하지 않아도 된다.
		virtual void ReleaseSocket(socket_t _socket) noexcept {}
	};
END_NS
