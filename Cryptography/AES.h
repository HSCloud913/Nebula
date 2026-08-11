#pragma once
#include <cstddef>
#include "Base/Type.h"
#include "Cryptography/Diagnostic/Error.h"

namespace ne::crypto
{
	/**
	 * @class AES
	 * @brief AES(128/192/256) 블록 암호의 CBC 모드 암·복호를 제공합니다.
	 *
	 * **인증(MAC)은 하지 않습니다.** 인증 없는 암호문은 변조를 탐지할 수 없고, 복호 실패를 원격에
	 * 오라클로 흘리면 평문이 복구될 수 있습니다(Vaudenay 패딩 오라클). 따라서 신뢰할 수 없는 경로로
	 * 주고받는 데이터라면 **암호문 + IV 전체에 HMAC 을 걸고(Encrypt-then-MAC), 복호 전에 MAC 을
	 * 상수시간으로 먼저 검증**해야 합니다(HMACKey 참고).
	 *
	 * @warning 자체 구현이며 사이드채널(캐시 타이밍) 내성이 검증되지 않았다. 인증 모드(GCM)는 미제공.
	 *          소멸 시 보관 중인 키를 소거하지만 best-effort 다(Util/SecureWipe.h 참고).
	 */
	class AES final
	{
	public:
		enum class Type
		{
			AES_128, // 키 16바이트
			AES_192, // 키 24바이트
			AES_256  // 키 32바이트
		};

	private:
		explicit AES(Type _type, string_t _key) noexcept;

	public:
		~AES();

		NEBULA_NON_COPYABLE(AES)
		NEBULA_DEFAULT_MOVE(AES)

	public:
		static constexpr std::size_t BlockSize = 16;

	public:
		/**
		 * @class CbcCiphertext
		 * @brief IV 자동 생성 판 EncryptCBC 의 결과 — 복호에는 두 값이 모두 필요합니다.
		 */
		struct CbcCiphertext
		{
			string_t iv;
			string_t ciphertext;
		};

	private:
		Type type;
		string_t key;

	public:
		/**
		 * @brief _type 에 맞는 길이의 _key 로 AES 를 만듭니다.
		 * @return 키 길이가 _type 의 요구(16/24/32바이트)와 다르면 INVALID_KEY_LENGTH.
		 */
		[[nodiscard]] static CryptoResult<AES> Create(Type _type, string_view_t _key);

		/** @brief _type 이 요구하는 키 길이(바이트)입니다. */
		[[nodiscard]] static constexpr std::size_t KeyLength(const Type _type) noexcept
		{
			switch (_type)
			{
				case Type::AES_128: return 16;
				case Type::AES_192: return 24;
				case Type::AES_256: return 32;
			}

			return 0;
		}

	public:
		/**
		 * @brief CBC 모드로 암호화합니다(PKCS#7 패딩).
		 * @param _iv 16바이트 초기화 벡터. 같은 키로 재사용하면 안 됩니다 — 매번 예측 불가능한 새 값이어야 합니다.
		 * @return _iv 가 16바이트가 아니면 INVALID_INPUT.
		 */
		[[nodiscard]] CryptoResult<string_t> EncryptCBC(string_view_t _iv, string_view_t _plaintext) const;

		/**
		 * @brief IV 를 CSPRNG 로 새로 뽑아 CBC 암호화합니다 — IV 재사용 실수를 구조적으로 막는 권장 오버로드입니다.
		 * @param _plaintext 암호화할 평문(길이 제약 없음 — PKCS#7 로 패딩됩니다).
		 * @return 생성한 IV 와 암호문. CSPRNG 실패 시 RANDOM_FAILURE.
		 */
		[[nodiscard]] CryptoResult<CbcCiphertext> EncryptCBC(string_view_t _plaintext) const;

		/**
		 * @brief CBC 모드로 복호화합니다.
		 * @return _iv 길이 오류·암호문이 블록 배수가 아니면 INVALID_INPUT, 패딩이 깨졌으면 MALFORMED_PADDING.
		 * @warning 이 실패 사유를 그대로 원격 피어에 노출하면 패딩 오라클이 됩니다. 인증이 필요한
		 *          경로에서는 이 함수를 직접 쓰지 말고 Open() 을 쓰세요.
		 */
		[[nodiscard]] CryptoResult<string_t> DecryptCBC(string_view_t _iv, string_view_t _ciphertext) const;
	};

	/** @brief IV 를 CSPRNG 로 새로 뽑아 CBC 암호화합니다. 키 길이가 _type 과 다르면 INVALID_KEY_LENGTH. */
	[[nodiscard]] CryptoResult<AES::CbcCiphertext> AesEncrypt(AES::Type _type, string_view_t _key, string_view_t _plaintext);

	/** @brief CBC 복호화합니다. 패딩이 깨졌으면 MALFORMED_PADDING(원격에 사유를 노출하면 패딩 오라클이 됩니다). */
	[[nodiscard]] CryptoResult<string_t> AesDecrypt(AES::Type _type, string_view_t _key, string_view_t _iv, string_view_t _ciphertext);
}
