//
// Created by hscloud on 24. 11. 10.
//

#include "Cryptography/Internal/HashFactory.h"

#include "Cryptography/Internal/Algorithm/CRC32.h"
#include "Cryptography/Internal/Algorithm/MD5.h"
#include "Cryptography/Internal/Algorithm/SHA1.h"
#include "Cryptography/Internal/Algorithm/SHA2.h"
#include "Cryptography/Internal/Algorithm/SHA3.h"



namespace ne::crypto::internal
{
	std::unique_ptr<HashWrapper> HashFactory::Create(const HashType _hashType)
	{
		using enum HashType;
		switch (_hashType)
		{
			case CRC32:
				return std::make_unique<HashAdapter<internal::CRC32>>();
			case MD5:
				return std::make_unique<HashAdapter<internal::MD5>>();
			case SHA1:
				return std::make_unique<HashAdapter<internal::SHA1>>();
			case SHA2_224:
				return std::make_unique<HashAdapter<SHA2>>(SHA2::Type::SHA2_224);
			case SHA2_256:
				return std::make_unique<HashAdapter<SHA2>>(SHA2::Type::SHA2_256);
			case SHA2_384:
				return std::make_unique<HashAdapter<SHA2>>(SHA2::Type::SHA2_384);
			case SHA2_512:
				return std::make_unique<HashAdapter<SHA2>>(SHA2::Type::SHA2_512);
			case SHA3_224:
				return std::make_unique<HashAdapter<SHA3>>(SHA3::Type::SHA3_224);
			case SHA3_256:
				return std::make_unique<HashAdapter<SHA3>>(SHA3::Type::SHA3_256);
			case SHA3_384:
				return std::make_unique<HashAdapter<SHA3>>(SHA3::Type::SHA3_384);
			case SHA3_512:
				return std::make_unique<HashAdapter<SHA3>>(SHA3::Type::SHA3_512);
		}

		return nullptr;
	}
}
