#pragma once
#include <memory>
#include "Hash/Hash.h"
#include "Hash/Algorithm/Wrapper/HashWrapper.h"

BEGIN_NS(ne::crypto)
	class HashFactory final
	{
	private:
		explicit HashFactory() = default;
		~HashFactory() = default;

	public:
		NEBULA_NON_COPYABLE_MOVABLE(HashFactory)

	public:
		static std::unique_ptr<HashWrapper> Create(HashType _hashType);
	};

END_NS
