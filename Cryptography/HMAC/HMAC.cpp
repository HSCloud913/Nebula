#include "HMAC.h"



BEGIN_NS(ne::cryptography)

static size_t GetBlockSize(HashType _type)
{
	switch (_type)
	{
		case HashType::SHA2_384:
		case HashType::SHA2_512:
			return 128;
		case HashType::SHA3_224:
			return 144;
		case HashType::SHA3_256:
			return 136;
		case HashType::SHA3_384:
			return 104;
		case HashType::SHA3_512:
			return 72;
		default:
			return 64;
	}
}

static string_t HexToBytes(const string_t& _hex)
{
	string_t bytes;
	bytes.reserve(_hex.size() / 2);

	for (size_t i = 0; i + 1 < _hex.size(); i += 2)
	{
		const char_t hi = _hex[i];
		const char_t lo = _hex[i + 1];

		auto fromHex = [](char_t c) -> byte_t
		{
			if (c >= '0' && c <= '9') return static_cast<byte_t>(c - '0');
			if (c >= 'a' && c <= 'f') return static_cast<byte_t>(c - 'a' + 10);
			return static_cast<byte_t>(c - 'A' + 10);
		};

		bytes += static_cast<char_t>((fromHex(hi) << 4) | fromHex(lo));
	}

	return bytes;
}

string_t HMAC::Compute(HashType _type, string_t&& _key, string_t&& _message)
{
	const size_t blockSize = GetBlockSize(_type);

	string_t normalizedKey;
	if (_key.size() > blockSize)
		normalizedKey = HexToBytes(HashFactory::Create(_type)->GetHashFromString(std::move(_key)));
	else
		normalizedKey = std::move(_key);
	normalizedKey.resize(blockSize, '\0');

	string_t ipad(blockSize, '\0');
	string_t opad(blockSize, '\0');
	for (size_t i = 0; i < blockSize; ++i)
	{
		ipad[i] = static_cast<char_t>(static_cast<byte_t>(normalizedKey[i]) ^ 0x36);
		opad[i] = static_cast<char_t>(static_cast<byte_t>(normalizedKey[i]) ^ 0x5C);
	}

	const string_t innerHashBytes = HexToBytes(
		HashFactory::Create(_type)->GetHashFromString(ipad + _message));

	return HashFactory::Create(_type)->GetHashFromString(opad + innerHashBytes);
}

END_NS
