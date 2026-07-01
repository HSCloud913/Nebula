#pragma once
#include "Type.h"

BEGIN_NS(ne::crypto)
	struct RSAPublicKey
	{
		ne::string_t n;
		ne::string_t e;

		[[nodiscard]] ne::string_t Encrypt(ne::string_t&& _plaintext) const;
	};

	struct RSAPrivateKey
	{
		ne::string_t n;
		ne::string_t d;

		[[nodiscard]] ne::string_t Decrypt(ne::string_t&& _ciphertext) const;
	};

	struct RSAKeyPair
	{
		enum class KeySize
		{
			RSA512 = 512,
			RSA1024 = 1024,
			RSA2048 = 2048
		};

		RSAPublicKey publicKey;
		RSAPrivateKey privateKey;

		[[nodiscard]] static RSAKeyPair Generate(KeySize _keySize = KeySize::RSA2048);
	};

END_NS
