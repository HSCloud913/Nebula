#ifndef NEBULA_HASHFACTORY_H
#define NEBULA_HASHFACTORY_H

#include <memory>
#include "Type.h"
#include "Hash/Wrapper/HashWrapper.h"

BEGIN_NS(ne::cryptography)
	enum class HashType
	{
		CRC32,
		MD5,
		SHA1,
		SHA2_224,
		SHA2_256,
		SHA2_384,
		SHA2_512,
		SHA3_224,
		SHA3_256,
		SHA3_384,
		SHA3_512
	};

	class HashFactory final
	{
		NEBULA_NON_COPYABLE_MOVABLE(HashFactory)

	private:
		explicit HashFactory() = default;
		~HashFactory() = default;

	public:
		static std::unique_ptr<HashWrapper> Create(HashType _hashType);
	};

END_NS

#endif //NEBULA_HASHFACTORY_H