#pragma once
#include "Base/Type.h"
#include "Cryptography/Hash/Hash.h"

BEGIN_NS(ne::crypto)
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
