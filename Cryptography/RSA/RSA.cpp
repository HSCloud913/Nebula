#include "RSA.h"
#include "BigInt.h"

#include <random>
#include <stdexcept>
#include <chrono>



BEGIN_NS(ne::cryptography)
	RSAKeyPair RSA::GenerateKeyPair(KeySize _keySize)
	{
		const size_t halfBits = static_cast<size_t>(_keySize) / 2;

		// Seed with time + hardware randomness
		std::random_device rd;
		const ulonglong_t seed = rd()
								^ static_cast<ulonglong_t>(std::chrono::steady_clock::now().time_since_epoch().count());
		std::mt19937_64 rng(seed);

		BigInt n, d;
		const BigInt e(65537u);

		while (true)
		{
			BigInt p = BigInt::RandomPrime(halfBits, rng);
			BigInt q = BigInt::RandomPrime(halfBits, rng);
			if (p == q) continue;

			n = p * q;
			if (n.BitLength() != static_cast<size_t>(_keySize)) continue;

			BigInt phi = (p - BigInt(1u)) * (q - BigInt(1u));
			if (BigInt::Gcd(e, phi) != BigInt(1u)) continue;

			d = BigInt::ModInverse(e, phi);
			break;
		}

		RSAKeyPair kp;
		kp.publicKey.n = n.ToHex();
		kp.publicKey.e = e.ToHex();
		kp.privateKey.n = n.ToHex();
		kp.privateKey.d = d.ToHex();

		return kp;
	}

	string_t RSA::Encrypt(const RSAPublicKey& _key, string_t&& _plaintext)
	{
		const BigInt n = BigInt::FromHex(_key.n);
		const BigInt e = BigInt::FromHex(_key.e);

		const size_t keyLength = (n.BitLength() + 7) / 8;
		if (_plaintext.size() > keyLength - 11)
		{
			throw std::invalid_argument("RSA::Encrypt: plaintext too large for key size");
		}

		// PKCS#1 v1.5 type 2 encryption padding
		std::random_device rd;
		std::mt19937_64 rng(rd());
		std::uniform_int_distribution<uint_t> dist(1, 255);

		const size_t psLen = keyLength - 3 - _plaintext.size();

		string_t em;
		em.reserve(keyLength);
		em += '\x00';
		em += '\x02';
		for (size_t i = 0; i < psLen; ++i)
		{
			em += static_cast<char_t>(dist(rng));
		}
		em += '\x00';
		em += _plaintext;

		const BigInt m = BigInt::FromBytes(em);
		const BigInt c = BigInt::ModPow(m, e, n);

		return c.ToBytes(keyLength);
	}

	string_t RSA::Decrypt(const RSAPrivateKey& _key, string_t&& _ciphertext)
	{
		const BigInt n = BigInt::FromHex(_key.n);
		const BigInt d = BigInt::FromHex(_key.d);
		const size_t keyLen = (n.BitLength() + 7) / 8;

		const BigInt c = BigInt::FromBytes(_ciphertext);
		const BigInt m = BigInt::ModPow(c, d, n);
		const string_t em = m.ToBytes(keyLen);

		// PKCS#1 v1.5 unpadding: 0x00 0x02 PS 0x00 M
		if (em.size() < 11 || static_cast<byte_t>(em[0]) != 0x00 || static_cast<byte_t>(em[1]) != 0x02)
		{
			throw std::runtime_error("RSA::Decrypt: invalid padding");
		}

		size_t sep = 2;
		while (sep < em.size() && em[sep] != '\x00') ++sep;

		if (sep == em.size()) throw std::runtime_error("RSA::Decrypt: padding separator not found");

		return em.substr(sep + 1);
	}

END_NS
