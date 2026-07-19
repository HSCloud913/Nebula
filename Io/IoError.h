//
// Created by hscloud on 26. 7. 7.
//

#pragma once
#include "Base/Type.h"
#include "Base/Error.h"

BEGIN_NS(ne::io)
	/**
	 * @class IoErrorKind
	 * @brief 등록 버퍼/capability 경로 전용 에러 분류 열거형.
	 */
	enum class IoErrorKind : byte_t
	{
		UNSUPPORTED,
		REGISTRATION_LIMIT_EXCEEDED,
		INVALID_BUFFER,
		QUEUE_FULL,
		OS_FAILURE,
	};

	/**
	 * @class IoError
	 * @brief ne::Error 를 상속한 Io 계층 전용 에러 타입.
	 *
	 * IoErrorKind 로 에러 종류를 분류하며, 하위 OS 실패(OsError)를 감쌀 경우 OS 에러 코드를
	 * 보존한다. Context()/What() 및 Result<T, IoError> 와 맞물려 값 기반 에러 전파에 쓰인다.
	 */
	class IoError :public ne::Error
	{
	public:
		explicit IoError(const IoErrorKind _kind, const string_view_t _message = {})
			: Error(_message.empty() ? DefaultMessage(_kind) : string_t{ _message })
			, kind(_kind) {}

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
				case IoErrorKind::UNSUPPORTED:
					return "capability not supported by this engine";
				case IoErrorKind::REGISTRATION_LIMIT_EXCEEDED:
					return "registered buffer limit exceeded";
				case IoErrorKind::INVALID_BUFFER:
					return "invalid or unregistered buffer";
				case IoErrorKind::QUEUE_FULL:
					return "request/completion queue full";
				case IoErrorKind::OS_FAILURE:
					return "os failure";
			}

			return "unknown io error";
		}
	};

END_NS
