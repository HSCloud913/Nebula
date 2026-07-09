#include "Cryptography/Hash/Hash.h"
#include "Cryptography/Hash/Factory/HashFactory.h"



BEGIN_NS(ne::crypto)
	string_t Hash(const HashType _type, string_t&& _data)
	{
		return HashFactory::Create(_type)->GetHashFromString(std::move(_data));
	}

	string_t HashFile(const HashType _type, string_t&& _path)
	{
		return HashFactory::Create(_type)->GetHashFromFile(std::move(_path));
	}

END_NS
