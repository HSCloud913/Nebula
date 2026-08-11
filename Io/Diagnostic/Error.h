//
// Created by hscloud on 26. 7. 7.
//

#pragma once
#include <utility>
#include "Base/Type.h"
#include "Base/Error.h"
#include "Base/Result.h"

namespace ne::io
{
	/**
	 * @class IoErrorKind
	 * @brief Io 실패의 이식 가능한 분류 열거형.
	 *
	 * OS 에러 코드는 플랫폼마다 다르므로(WSAECONNRESET vs ECONNRESET, ERROR_OPERATION_ABORTED vs
	 * ECANCELED), 이식 가능한 사용자 코드가 분기할 수 있는 최소 어휘를 여기서 정의한다. 원본 코드는
	 * IoError::Code() 로 계속 보존된다.
	 */
	enum class IoErrorKind : byte_t
	{
		UNSUPPORTED,
		REGISTRATION_LIMIT_EXCEEDED,
		INVALID_BUFFER,
		CANCELLED,          // stop_token 취소 또는 명시적 Cancel — 재시도 대상이 아니다
		CONNECTION_CLOSED,  // 상대가 연결을 닫음/리셋함
		OS_FAILURE,
	};

	/** @brief 플랫폼 에러 코드를 이식 가능한 IoErrorKind 로 정규화한다. 대응이 없으면 OS_FAILURE. */
	[[nodiscard]] IoErrorKind ClassifyOsError(ne::ulong_t _code) noexcept;

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

		// OS 실패도 분류를 채워 둔다 — 그래야 IsCancelled()/IsConnectionClosed() 가 플랫폼과 무관하게 쓰인다.
		explicit IoError(const ne::OsError& _os)
			: Error(_os.What())
			, kind(ClassifyOsError(_os.Code()))
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
		[[nodiscard]] bool_t IsCancelled() const noexcept { return kind == IoErrorKind::CANCELLED; }
		[[nodiscard]] bool_t IsConnectionClosed() const noexcept { return kind == IoErrorKind::CONNECTION_CLOSED; }

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
				case IoErrorKind::CANCELLED:
					return "operation cancelled";
				case IoErrorKind::CONNECTION_CLOSED:
					return "connection closed by peer";
				case IoErrorKind::OS_FAILURE:
					return "os failure";
			}

			return "unknown io error";
		}
	};

	template <typename T>
	using IoResult = ne::Result<T, IoError>;
}
