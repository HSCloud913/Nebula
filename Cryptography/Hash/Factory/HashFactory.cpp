#include "HashFactory.h"

#include "Hash/Wrapper/CRC32Wrapper.h"
#include "Hash/Wrapper/MD5Wrapper.h"
#include "Hash/Wrapper/SHA1Wrapper.h"
#include "Hash/Wrapper/SHA2Wrapper.h"
#include "Hash/Wrapper/SHA3Wrapper.h"



BEGIN_NS(ne::cryptography)
	std::unique_ptr<HashWrapper> HashFactory::Create(HashType _hashType)
	{
		using enum HashType;
		switch (_hashType)
		{
		case CRC32: return std::make_unique<CRC32Wrapper>();
		case MD5: return std::make_unique<MD5Wrapper>();
		case SHA1: return std::make_unique<SHA1Wrapper>();
		case SHA2_224: return std::make_unique<SHA2Wrapper>(SHA2::Type::SHA2_224);
		case SHA2_256: return std::make_unique<SHA2Wrapper>(SHA2::Type::SHA2_256);
		case SHA2_384: return std::make_unique<SHA2Wrapper>(SHA2::Type::SHA2_384);
		case SHA2_512: return std::make_unique<SHA2Wrapper>(SHA2::Type::SHA2_512);
		case SHA3_224: return std::make_unique<SHA3Wrapper>(SHA3::Type::SHA3_224);
		case SHA3_256: return std::make_unique<SHA3Wrapper>(SHA3::Type::SHA3_256);
		case SHA3_384: return std::make_unique<SHA3Wrapper>(SHA3::Type::SHA3_384);
		case SHA3_512: return std::make_unique<SHA3Wrapper>(SHA3::Type::SHA3_512);
		}

		return nullptr;
	}

END_NS
