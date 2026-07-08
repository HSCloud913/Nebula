//
// Created by hscloud on 26. 7. 7.
//

#pragma once
#include "Type.h"

// Base/Error.h 가 windef.h/errhandlingapi.h/winbase.h 를 windows.h 없이 직접 include 하는데,
// 이 헤더가 번역 단위에서 가장 먼저 Windows SDK 헤더를 건드리면 winnt.h 가 필요로 하는
// 아키텍처 매크로(_AMD64_ 등)가 아직 없어 "No Target Architecture"(C1189) 로 빌드가 깨진다.
// Base/Error.h 자체는 건드리지 않고, 여기서 windows.h 를 먼저 포함해 그 전처리기 preamble이
// 항상 먼저 실행되도록 한다 — windef.h 등은 include guard 로 이미 처리된 상태라 이후
// Base/Error.h 의 include 는 아무 효과 없는 안전한 중복이 된다.
#if defined(_WIN32)
#	include <windows.h>
#endif

#include <cstdint>
#include "Error.h"

BEGIN_NS(ne::io)
	// 등록 버퍼/capability 경로 전용 에러 분류. 기존 ne::Error 체계를 상속해
	// Context()/What() 및 Result<T, IoError> 와 그대로 맞물린다(OsError/HttpError 와 동형).
	enum class IoErrorKind : uint8_t
	{
		UNSUPPORTED,               // 엔진이 해당 capability 미지원 (예: epoll 에 RegisteredIo)
		REGISTRATION_LIMIT_EXCEEDED, // RIO / io_uring 버퍼 등록 한도 초과
		INVALID_BUFFER,             // 미등록 버퍼이거나 등록 영역 밖 sub-view
		QUEUE_FULL,                 // RIO_RQ/RIO_CQ 또는 SQ 용량 초과
		OS_FAILURE,                 // 하위 syscall 실패 — Code() 유효
	};

	class IoError :public ne::Error
	{
	public:
		explicit IoError(const IoErrorKind _kind, const string_view_t _message = {})
			: Error(_message.empty() ? DefaultMessage(_kind) : string_t{ _message })
			, kind(_kind) {}

		// 하위 OS 실패를 감싼다 — Kind::OsFailure 로 두고 OS 코드/메시지를 보존한다.
		explicit IoError(const ne::OsError& _os)
			: Error(_os.What())
			, code(_os.Code()) {}

	private:
		IoErrorKind kind{ IoErrorKind::OS_FAILURE };
		ne::ulong_t code{ 0 };

	public:
		IoError& Context(const string_view_t _context)
		{
			Error::Context(_context);
			return *this;
		}

		[[nodiscard]] IoErrorKind Kind() const noexcept { return kind; }
		[[nodiscard]] ne::ulong_t Code() const noexcept { return code; }
		[[nodiscard]] bool_t IsUnsupported() const noexcept { return kind == IoErrorKind::UNSUPPORTED; }

	private:
		[[nodiscard]] static string_t DefaultMessage(const IoErrorKind _kind)
		{
			switch (_kind)
			{
			case IoErrorKind::UNSUPPORTED: return "capability not supported by this engine";
			case IoErrorKind::REGISTRATION_LIMIT_EXCEEDED: return "registered buffer limit exceeded";
			case IoErrorKind::INVALID_BUFFER: return "invalid or unregistered buffer";
			case IoErrorKind::QUEUE_FULL: return "request/completion queue full";
			case IoErrorKind::OS_FAILURE: return "os failure";
			}

			return "unknown io error";
		}
	};

END_NS
