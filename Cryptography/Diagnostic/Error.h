//
// Created by hscloud on 26. 8. 6.
//

#pragma once
#include "Base/Type.h"
#include "Base/Error.h"
#include "Base/Result.h"

namespace ne::crypto
{
	/**
	 * @class CryptoErrorKind
	 * @brief 암호 계층 에러 분류 열거형.
	 */
	enum class CryptoErrorKind : byte_t
	{
		INVALID_KEY_LENGTH,     // 키 길이가 알고리즘 요구와 다름
		INVALID_INPUT,          // 블록 정렬 위반, 봉투가 너무 짧음 등 형식 오류
		AUTHENTICATION_FAILED,  // MAC 불일치 — 변조되었거나 키가 다름
		UNSUPPORTED_VERSION,    // 봉투 버전을 이 빌드가 모름
		RANDOM_FAILURE,         // OS CSPRNG 호출 실패(엔트로피 확보 불가)
		MALFORMED_PADDING,      // PKCS#7 패딩이 깨짐
		MESSAGE_TOO_LARGE,      // 키 크기 대비 평문이 너무 큼(RSA)
	};

	/**
	 * @class CryptoError
	 * @brief ne::Error 를 상속한 암호 계층 전용 에러 타입입니다(HttpError/IoError 와 동일한 패턴).
	 *
	 * @note AUTHENTICATION_FAILED 는 "변조" 와 "키 불일치" 를 구분하지 않습니다 — 어느 쪽인지
	 *       알려주는 것 자체가 공격자에게 정보를 주기 때문입니다. 같은 이유로 Open() 은 MAC 검증
	 *       실패 시 복호를 시도하지 않으므로 MALFORMED_PADDING 을 절대 반환하지 않습니다.
	 */
	class CryptoError final :public ne::Error
	{
	public:
		explicit CryptoError(const CryptoErrorKind _kind, const string_view_t _message = {})
			: Error(_message.empty() ? DefaultMessage(_kind) : string_t{ _message })
			, kind(_kind) {}

	private:
		CryptoErrorKind kind;

	public:
		CryptoError& Context(const string_view_t _context)
		{
			Error::Context(_context);
			return *this;
		}

		[[nodiscard]] CryptoErrorKind Kind() const noexcept { return kind; }

	private:
		[[nodiscard]] static string_t DefaultMessage(const CryptoErrorKind _kind)
		{
			switch (_kind)
			{
				case CryptoErrorKind::INVALID_KEY_LENGTH:
					return "invalid key length";
				case CryptoErrorKind::INVALID_INPUT:
					return "malformed input";
				case CryptoErrorKind::AUTHENTICATION_FAILED:
					return "authentication failed";
				case CryptoErrorKind::UNSUPPORTED_VERSION:
					return "unsupported envelope version";
				case CryptoErrorKind::RANDOM_FAILURE:
					return "secure random generation failed";
				case CryptoErrorKind::MALFORMED_PADDING:
					return "malformed PKCS#7 padding";
				case CryptoErrorKind::MESSAGE_TOO_LARGE:
					return "message too large for key size";
			}

			return "unknown crypto error";
		}
	};

	template <typename T>
	using CryptoResult = ne::Result<T, CryptoError>;
}
