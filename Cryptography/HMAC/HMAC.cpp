#include "Cryptography/HMAC/HMAC.h"

#include "Cryptography/Hash/Factory/HashFactory.h"



inline size_t GetBlockSize(const ne::crypto::HashType _type)
{
	switch (_type)
	{
		case ne::crypto::HashType::SHA2_384:
		case ne::crypto::HashType::SHA2_512:
			return 128;
		case ne::crypto::HashType::SHA3_224:
			return 144;
		case ne::crypto::HashType::SHA3_256:
			return 136;
		case ne::crypto::HashType::SHA3_384:
			return 104;
		case ne::crypto::HashType::SHA3_512:
			return 72;
		default:
			return 64;
	}
}

inline ne::string_t HexToBytes(const ne::string_t& _hex)
{
	ne::string_t bytes;
	bytes.reserve(_hex.size() / 2);

	for (size_t i = 0; i + 1 < _hex.size(); i += 2)
	{
		const ne::char_t hi = _hex[i];
		const ne::char_t lo = _hex[i + 1];

		auto fromHex = [](const ne::char_t c) -> ne::byte_t
		{
			if (c >= '0' && c <= '9') return static_cast<ne::byte_t>(c - '0');
			if (c >= 'a' && c <= 'f') return static_cast<ne::byte_t>(c - 'a' + 10);
			return static_cast<ne::byte_t>(c - 'A' + 10);
		};

		bytes += static_cast<ne::char_t>((fromHex(hi) << 4) | fromHex(lo));
	}

	return bytes;
}



BEGIN_NS(ne::crypto)
	HMACKey::HMACKey(const HashType _type, string_t&& _ipad, string_t&& _opad) noexcept
		: type(_type)
		, ipad(std::move(_ipad))
		, opad(std::move(_opad)) {}



	HMACKey HMACKey::Create(const HashType _type, string_t&& _key)
	{
		const size_t blockSize = GetBlockSize(_type);

		string_t normalizedKey = (_key.size() > blockSize) ? HexToBytes(HashFactory::Create(_type)->GetHashFromString(std::move(_key))) : std::move(_key);
		normalizedKey.resize(blockSize, '\0');

		string_t ipad(blockSize, '\0');
		string_t opad(blockSize, '\0');
		for (size_t i = 0; i < blockSize; ++i)
		{
			ipad[i] = static_cast<char_t>(static_cast<byte_t>(normalizedKey[i]) ^ 0x36);
			opad[i] = static_cast<char_t>(static_cast<byte_t>(normalizedKey[i]) ^ 0x5C);
		}

		return HMACKey(_type, std::move(ipad), std::move(opad));
	}



	string_t HMACKey::Generate(string_t&& _message) const
	{
		const string_t innerHashBytes = HexToBytes(HashFactory::Create(type)->GetHashFromString(ipad + _message));

		return HashFactory::Create(type)->GetHashFromString(opad + innerHashBytes);
	}

END_NS
