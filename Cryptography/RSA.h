#pragma once
#include "Base/Type.h"
#include "Cryptography/Hash.h"
#include "Cryptography/Diagnostic/Error.h"

namespace ne::crypto
{
	/** @brief RSA 공개키(n, e — 16진 문자열)로 암호화·서명 검증을 수행합니다. */
	struct RSAPublicKey
	{
		ne::string_t n;
		ne::string_t e;

		/**
		 * @brief PKCS#1 v1.5 패딩으로 암호화합니다.
		 * @param _plainText 암호화할 평문.
		 * @return 평문이 (키 크기 - 11)바이트를 넘으면 MESSAGE_TOO_LARGE, CSPRNG 실패 시 RANDOM_FAILURE.
		 */
		[[nodiscard]] CryptoResult<ne::string_t> Encrypt(ne::string_view_t _plainText) const;

		/**
		 * @brief RSASSA-PKCS1-v1_5 서명을 검증합니다(해시는 _hashType, 기본 SHA-256).
		 * @return 서명이 유효하면 true. 위조·형식 오류·키 불일치는 모두 false 로 뭉개 반환합니다
		 *         (어느 쪽인지 알려주는 것 자체가 공격자에게 정보를 줍니다).
		 * @note 서명 대상은 _message 자체이며, 내부에서 해시해 DigestInfo 를 재구성해 비교합니다.
		 */
		[[nodiscard]] bool_t Verify(ne::string_view_t _message, ne::string_view_t _signature, HashType _hashType = HashType::SHA2_256) const;

		/** @brief "NRSA1-PUB:<n>:<e>" 형식 문자열로 직렬화합니다(저장·교환용). */
		[[nodiscard]] ne::string_t Serialize() const;

		/**
		 * @brief Serialize() 결과를 되읽습니다.
		 * @return 태그가 다르거나 16진이 아니면 INVALID_INPUT, 버전이 다르면 UNSUPPORTED_VERSION.
		 */
		[[nodiscard]] static CryptoResult<RSAPublicKey> Deserialize(ne::string_view_t _text);
	};

	/**
	 * @brief RSA 개인키(n, d — 16진 문자열)로 복호화·서명을 수행합니다.
	 * @note 소멸 시 d(비밀 지수)를 소거합니다 — best-effort 이며, 이 구조체를 복사해 두면 그 사본은
	 *       각자 소멸 시점에 소거됩니다(Util/SecureWipe.h 참고).
	 */
	struct RSAPrivateKey
	{
		ne::string_t n;
		ne::string_t d;

		~RSAPrivateKey();

		/**
		 * @brief PKCS#1 v1.5 패딩을 벗겨 복호화합니다.
		 * @param _cipherText 복호화할 암호문.
		 * @return 패딩 구조가 깨졌으면 MALFORMED_PADDING.
		 * @warning 이 실패 사유를 원격 피어에 그대로 노출하면 패딩 오라클(Bleichenbacher)이 됩니다 —
		 *          호출자 쪽에서 단일한 "실패" 로 뭉개 응답하세요.
		 */
		[[nodiscard]] CryptoResult<ne::string_t> Decrypt(ne::string_view_t _cipherText) const;

		/**
		 * @brief RSASSA-PKCS1-v1_5 로 _message 에 서명합니다(해시는 _hashType, 기본 SHA-256).
		 * @return 서명 바이트열(키 크기와 같은 길이). 키가 DigestInfo+패딩을 담기에 너무 작으면 MESSAGE_TOO_LARGE.
		 * @note 결정적 서명입니다(PKCS#1 v1.5 는 난수를 쓰지 않음) — 같은 키·메시지면 항상 같은 서명.
		 *       확률적 패딩(PSS)은 미제공.
		 */
		[[nodiscard]] CryptoResult<ne::string_t> Sign(ne::string_view_t _message, HashType _hashType = HashType::SHA2_256) const;

		/** @brief "NRSA1-PRV:<n>:<d>" 형식 문자열로 직렬화합니다 — **비밀 값이므로 취급에 주의하세요.** */
		[[nodiscard]] ne::string_t Serialize() const;

		/** @brief Serialize() 결과를 되읽습니다(실패 사유는 RSAPublicKey::Deserialize 와 동일). */
		[[nodiscard]] static CryptoResult<RSAPrivateKey> Deserialize(ne::string_view_t _text);
	};

	/**
	 * @class RSAKeyPair
	 * @brief RSA 공개키/개인키 쌍을 생성하는 팩토리입니다.
	 *
	 * @warning 자체 구현이며 프로덕션 사용에 적합하지 않다. 패딩이 PKCS#1 v1.5 뿐이라 OAEP/PSS 를
	 *          제공하지 않고, 복호 시 패딩 검사가 상수시간이 아니어서 패딩 오라클(Bleichenbacher)
	 *          공격에 취약하다. 대칭키로 충분한 경우(대부분)에는 AES+HMAC 조합이 훨씬 안전하다.
	 * @note 소수 탐색을 반복하므로 키 생성이 느리다(1024비트에 수십 초). 512비트는 실제로 소인수분해가
	 *       가능한 크기여서 2026-08-06 에 선택지에서 제거했다.
	 */
	struct RSAKeyPair
	{
		enum class KeySize
		{
			RSA_1024 = 1024,
			RSA_2048 = 2048
		};

		RSAPublicKey publicKey;
		RSAPrivateKey privateKey;

		/** @brief 키 쌍을 생성합니다. 소수 탐색을 반복하므로 2048비트는 수 초 이상 걸릴 수 있습니다. */
		[[nodiscard]] static RSAKeyPair Generate(KeySize _keySize = KeySize::RSA_2048);
	};

	// ───────────────────────── 하이브리드 암호(권장 사용법) ─────────────────────────

	/**
	 * @brief 임의 길이 데이터를 공개키로 안전하게 봉인합니다 — RSA 로는 대칭키만 감싸고 본문은 AES 로 처리합니다.
	 *
	 * RSA 직접 암호화는 (키 크기 - 11)바이트라는 상한이 있고 큰 데이터에 지수적으로 느립니다. 이 함수는
	 * 표준적인 하이브리드 구성을 씁니다: CSPRNG 로 뽑은 임시 대칭키를 RSA 로 감싸고, 본문은
	 * AES-256-CBC 로 암호화한 뒤 전체에 HMAC-SHA256 을 겁니다(Encrypt-then-MAC).
	 *
	 * 봉투 레이아웃: `version(1) || wrappedKeyLength(2, big-endian) || wrappedKey || iv(16) || ciphertext || mac(32)`
	 * MAC 은 그 앞부분 전체(version..ciphertext) + _associatedData 를 덮습니다.
	 *
	 * @param _publicKey 봉인에 쓸 수신자 공개키.
	 * @param _plaintext 봉인할 평문(길이 제약 없음).
	 * @param _associatedData 봉투에 담기지 않지만 MAC 에 묶이는 문맥 정보 — RsaOpen 에 같은 값을 주지
	 *        않으면 인증이 실패하므로, 봉투를 다른 문맥으로 치환하는 것을 막습니다.
	 * @return 자기 완결 봉투 바이트열. CSPRNG 실패 시 RANDOM_FAILURE.
	 */
	[[nodiscard]] CryptoResult<ne::string_t> RsaSeal(const RSAPublicKey& _publicKey, ne::string_view_t _plaintext, ne::string_view_t _associatedData = {});

	/**
	 * @brief RsaSeal() 이 만든 봉투를 개인키로 열어 원본 평문을 돌려줍니다.
	 *
	 * 대칭키를 RSA 로 복원한 뒤 **MAC 을 상수시간으로 먼저 검증하고, 실패하면 본문 복호를 시도하지
	 * 않습니다** — 패딩 오라클이 성립할 여지를 없앱니다.
	 *
	 * @param _privateKey 봉인에 쓴 공개키와 짝인 개인키.
	 * @param _sealed RsaSeal() 이 만든 봉투.
	 * @param _associatedData 봉인 때 준 것과 같은 문맥 정보.
	 * @return 원본 평문. 봉투가 잘렸거나 형식이 깨지면 INVALID_INPUT, 버전을 모르면 UNSUPPORTED_VERSION,
	 *         변조·키 불일치·_associatedData 불일치는 모두 AUTHENTICATION_FAILED(구분해 알려주지 않음).
	 */
	[[nodiscard]] CryptoResult<ne::string_t> RsaOpen(const RSAPrivateKey& _privateKey, ne::string_view_t _sealed, ne::string_view_t _associatedData = {});
}
