#pragma once
#include <memory>
#include "Base/Type.h"
#include "Cryptography/Hash.h"
#include "Cryptography/Diagnostic/Error.h"

namespace ne::crypto::internal
{
	class HashWrapper;
}

namespace ne::crypto
{
	class HmacStream;

	/**
	 * @class HMACKey
	 * @brief 지정한 해시 알고리즘으로 HMAC 메시지 인증 코드를 생성하는 키입니다.
	 *
	 * Create()에서 원본 키를 ipad/opad로 미리 전처리해 보관하므로, Generate() 호출마다
	 * 매번 키를 재가공하지 않습니다. 소멸 시 전처리된 키를 소거합니다(best-effort).
	 */
	class HMACKey final
	{
	private:
		explicit HMACKey(HashType _type, string_t&& _ipad, string_t&& _opad) noexcept;

	public:
		~HMACKey();

		NEBULA_NON_COPYABLE(HMACKey)
		NEBULA_DEFAULT_MOVE(HMACKey)

	private:
		HashType type;
		string_t ipad;
		string_t opad;

	public:
		/**
		 * @brief _type 해시로 HMAC 키를 만듭니다.
		 * @return _type 이 암호학적 해시가 아니면(CRC32) INVALID_INPUT — CRC32 는 체크섬이라 MAC 으로
		 *         쓰면 위조를 막지 못합니다.
		 * @note MD5/SHA1 은 충돌 저항성이 깨졌지만 HMAC 구성에서는 아직 실용적 위조가 없어 허용합니다
		 *       (RFC 2202 벡터 검증·레거시 상호운용 목적). 새 설계에는 SHA2_256 이상을 쓰세요.
		 */
		[[nodiscard]] static CryptoResult<HMACKey> Create(HashType _type, string_view_t _key);

	public:
		/** @brief 메시지의 HMAC 을 소문자 hex 문자열로 계산합니다. */
		[[nodiscard]] string_t Generate(string_view_t _message) const;

		/** @brief 메시지의 HMAC 을 원시 바이트로 계산합니다(봉투에 담거나 서브키를 파생할 때). */
		[[nodiscard]] string_t GenerateBytes(string_view_t _message) const;

		/** @brief 메시지의 HMAC 을 계산해 예상 MAC(hex) 과 **상수시간** 비교한다(타이밍 공격 방지). `==` 로 직접 비교하지 말 것. */
		[[nodiscard]] bool_t Verify(string_view_t _message, string_view_t _expectedMac) const;

	public:
		/**
		 * @brief 이 키로 증분 계산기를 시작합니다 — 메시지를 조각째 넣어 전체 버퍼링 없이 MAC 을 계산합니다.
		 * @note 전처리된 키(ipad/opad)를 그대로 물려주므로, 같은 키로 여러 스트림을 만들어도 키 가공은 한 번뿐입니다.
		 */
		[[nodiscard]] CryptoResult<HmacStream> BeginStream() const;

		/** @brief 파일 전체의 HMAC 을 조각째 읽어 계산합니다(소문자 hex). 파일을 열 수 없으면 INVALID_INPUT. */
		[[nodiscard]] CryptoResult<string_t> GenerateFromFile(string_view_t _path) const;
	};

	/**
	 * @class HmacStream
	 * @brief 메시지를 조각째 넣어 HMAC 을 계산하는 증분 계산기입니다(대용량·스트리밍 입력용).
	 *
	 * @code
	 *   auto stream = key.BeginStream();
	 *   stream.Value().Update(chunk1);
	 *   stream.Value().Update(chunk2);
	 *   const ne::string_t mac = stream.Value().Final();   // 확정 후에는 Reset() 전까지 재사용 불가
	 * @endcode
	 *
	 * 결과는 같은 메시지를 한 번에 넣은 HMACKey::Generate() 와 정확히 같습니다.
	 */
	class HmacStream final
	{
		friend class HMACKey;

	private:
		HmacStream(HashType _type, string_t _ipad, string_t _opad, std::unique_ptr<internal::HashWrapper> _inner);

	public:
		~HmacStream();

		NEBULA_NON_COPYABLE(HmacStream)
		HmacStream(HmacStream&&) noexcept;
		HmacStream& operator=(HmacStream&&) noexcept;

	private:
		HashType type;
		string_t ipad;
		string_t opad;
		std::unique_ptr<internal::HashWrapper> inner; // ipad 까지 먹인 내부 해시의 진행 상태
		string_t finalized;                          // 확정된 MAC 바이트(반복 호출 시 재계산하지 않음)
		bool_t isFinalized{ false };

	public:
		/** @brief 메시지 조각을 이어 넣습니다. Final() 이후의 호출은 무시됩니다(Reset() 으로 다시 시작). */
		void_t Update(string_view_t _chunk);

		/** @brief 누적된 메시지의 MAC 을 소문자 hex 로 확정합니다. 반복 호출하면 같은 값을 돌려줍니다. */
		[[nodiscard]] string_t Final();

		/** @brief MAC 을 원시 바이트로 확정합니다(봉투에 담을 때). */
		[[nodiscard]] string_t FinalBytes();

		/** @brief 같은 키로 처음부터 다시 계산합니다(누적된 메시지 상태를 버림). */
		void_t Reset();
	};

	// ── 한 줄 진입점: 같은 키로 한 번만 계산할 때 씁니다. 반복 계산은 HMACKey::Create() 로 키를 재사용하세요. ──

	/** @brief _key 로 _message 의 HMAC 을 계산해 소문자 hex 로 반환합니다. _type 이 CRC32 면 INVALID_INPUT. */
	[[nodiscard]] CryptoResult<string_t> Hmac(HashType _type, string_view_t _key, string_view_t _message);

	/**
	 * @brief _message 의 HMAC 을 계산해 _expectedMac(hex) 과 **상수시간** 비교합니다.
	 * @return 일치하면 true. 불일치하거나 _type 이 MAC 에 부적합하면 false(실패 사유를 구분하지 않습니다).
	 */
	[[nodiscard]] bool_t HmacVerify(HashType _type, string_view_t _key, string_view_t _message, string_view_t _expectedMac);

	/**
	 * @brief 파일 전체의 HMAC 을 계산해 소문자 hex 로 반환합니다(전체를 메모리에 올리지 않음).
	 * @return 파일을 열 수 없으면 INVALID_INPUT, _type 이 CRC32 면 INVALID_INPUT.
	 */
	[[nodiscard]] CryptoResult<string_t> HmacFile(HashType _type, string_view_t _key, string_view_t _path);
}
