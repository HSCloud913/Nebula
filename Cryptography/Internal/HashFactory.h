//
// Created by hscloud on 24. 11. 10.
//

#pragma once
#include <memory>
#include "Cryptography/Hash.h"
#include "Cryptography/Internal/HashAdapter.h"

namespace ne::crypto::internal
{
	/**
	 * @class HashFactory
	 * @brief HashType 에 대응하는 해시 어댑터를 생성하는 정적 팩토리입니다(내부 전용).
	 *
	 * 공개 진입점은 Cryptography/Hash.h 의 Hash()/HashFile() 파사드이며, HMAC 처럼 해시를
	 * 반복 호출하는 내부 코드가 이 팩토리를 직접 씁니다.
	 */
	class HashFactory final
	{
	private:
		explicit HashFactory() = default;
		~HashFactory() = default;

	public:
		NEBULA_NON_COPYABLE_MOVABLE(HashFactory)

	public:
		[[nodiscard]] static std::unique_ptr<HashWrapper> Create(HashType _hashType);
	};
}
