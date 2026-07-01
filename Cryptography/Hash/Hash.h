#pragma once
#include "Type.h"

BEGIN_NS(ne::crypto)
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

	[[nodiscard]] string_t Hash(HashType _type, string_t&& _data);
	[[nodiscard]] string_t HashFile(HashType _type, string_t&& _path);
END_NS
