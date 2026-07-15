#pragma once
#include "Base/Type.h"
#include "Cryptography/Hash/Hash.h"

BEGIN_NS(ne::crypto)
	/**
	 * @class HMACKey
	 * @brief 지정한 해시 알고리즘으로 HMAC 메시지 인증 코드를 생성하는 키입니다.
	 *
	 * Create()에서 원본 키를 ipad/opad로 미리 전처리해 보관하므로, Generate() 호출마다
	 * 매번 키를 재가공하지 않습니다.
	 */
	class HMACKey final
	{
	private:
		explicit HMACKey(HashType _type, string_t&& _ipad, string_t&& _opad) noexcept;

	public:
		~HMACKey() = default;

		NEBULA_NON_COPYABLE_MOVABLE(HMACKey)

	private:
		HashType type;
		string_t ipad;
		string_t opad;

	public:
		[[nodiscard]] static HMACKey Create(HashType _type, string_t&& _key);

	public:
		[[nodiscard]] string_t Generate(string_t&& _message) const;
	};

END_NS
