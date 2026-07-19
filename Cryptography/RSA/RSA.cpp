#include "Cryptography/RSA/RSA.h"

#include <random>
#include <stdexcept>
#include "Cryptography/Math/BigInt.h"
#include "Cryptography/Random/SecureRandom.h"



BEGIN_NS(ne::crypto)
	string_t RSAPublicKey::Encrypt(string_t&& _plainText) const
	{
		const BigInt bn = BigInt::FromHex(n);
		const BigInt be = BigInt::FromHex(e);

		const size_t keyLength = (bn.BitLength() + 7) / 8;
		if (_plainText.size() > keyLength - 11) { throw std::invalid_argument("RSA::Encrypt: plaintext too large for key size"); }

		SecureRandom rng; // PKCS#1 v1.5 패딩 난수 — CSPRNG 사용(URBG 모델링이라 distribution 과 호환)
		std::uniform_int_distribution<uint_t> dist(1, 255);

		const size_t psLen = keyLength - 3 - _plainText.size();

		// PKCS#1 v1.5 패딩 레이아웃: 0x00 0x02 PS(0이 아닌 랜덤 바이트로 채운 패딩, 길이 가변) 0x00 M(원본 메시지)
		// - 0x00: 맨 앞 바이트가 0이면 BigInt로 변환해도 항상 keyLength 크기보다 작게 해석되지 않도록 보장.
		// - 0x02: 블록 타입(암호화용, 서명은 0x01) 식별자.
		// - PS: 0이 아닌 랜덤 바이트로 채워 최소 8바이트 이상 패딩 → 평문을 무작위화(같은 평문도 매번 다른 암호문)하고
		//   PS 중간에 0x00이 섞이면 안 되므로 1~255 범위에서만 뽑는다.
		// - 0x00: PS와 메시지를 구분하는 분리자.
		string_t em;
		em.reserve(keyLength);
		em += '\x00';
		em += '\x02';
		for (size_t i = 0; i < psLen; ++i) em += static_cast<char_t>(dist(rng));
		em += '\x00';
		em += _plainText;

		const BigInt m = BigInt::FromBytes(em);
		const BigInt c = BigInt::ModPow(m, be, bn);

		return c.ToBytes(keyLength);
	}



	string_t RSAPrivateKey::Decrypt(string_t&& _cipherText) const
	{
		const BigInt bn = BigInt::FromHex(n);
		const BigInt bd = BigInt::FromHex(d);
		const size_t keyLen = (bn.BitLength() + 7) / 8;

		const BigInt c = BigInt::FromBytes(_cipherText);
		const BigInt m = BigInt::ModPow(c, bd, bn);
		const string_t em = m.ToBytes(keyLen);

		if (em.size() < 11 || static_cast<byte_t>(em[0]) != 0x00 || static_cast<byte_t>(em[1]) != 0x02) { throw std::runtime_error("RSA::Decrypt: invalid padding"); }

		size_t sep = 2;
		while (sep < em.size() && em[sep] != '\x00') ++sep;

		if (sep == em.size()) { throw std::runtime_error("RSA::Decrypt: padding separator not found"); }

		return em.substr(sep + 1);
	}



	RSAKeyPair RSAKeyPair::Generate(const KeySize _keySize)
	{
		const size_t halfBits = static_cast<size_t>(_keySize) / 2;

		SecureRandom rng; // RSA 소수 p, q 생성 — 반드시 CSPRNG(예측 가능 시 키 복구 가능)

		BigInt n, d;
		const BigInt e(65537u);

		while (true)
		{
			BigInt p = BigInt::RandomPrime(halfBits, rng);
			BigInt q = BigInt::RandomPrime(halfBits, rng);
			if (p == q) continue;

			n = p * q;
			if (n.BitLength() != static_cast<size_t>(_keySize)) continue;

			const BigInt phi = (p - BigInt(1u)) * (q - BigInt(1u));
			if (BigInt::Gcd(e, phi) != BigInt(1u)) continue;

			d = BigInt::ModInverse(e, phi);
			break;
		}

		RSAKeyPair keyPair;
		keyPair.publicKey.n = n.ToHex();
		keyPair.publicKey.e = e.ToHex();
		keyPair.privateKey.n = n.ToHex();
		keyPair.privateKey.d = d.ToHex();

		return keyPair;
	}

END_NS
