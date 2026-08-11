#include "Cryptography/Hash.h"
#include "Cryptography/Internal/HashFactory.h"



namespace ne::crypto
{
	string_t Hash(const HashType _type, const string_view_t _data) { return internal::HashFactory::Create(_type)->FromString(_data); }

	string_t HashFile(const HashType _type, const string_view_t _path) { return internal::HashFactory::Create(_type)->FromFile(_path); }
}
