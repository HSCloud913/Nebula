#pragma once
#include "Base/Type.h"

BEGIN_NS(ne::crypto)
	struct RSAPublicKey
	{
		ne::string_t n;
		ne::string_t e;

		[[nodiscard]] ne::string_t Encrypt(ne::string_t&& _plainText) const;
	};

	struct RSAPrivateKey
	{
		ne::string_t n;
		ne::string_t d;

		[[nodiscard]] ne::string_t Decrypt(ne::string_t&& _cipherText) const;
	};

	struct RSAKeyPair
	{
		enum class KeySize
		{
			RSA_512  = 512,
			RSA_1024 = 1024,
			RSA_2048 = 2048
		};

		RSAPublicKey publicKey;
		RSAPrivateKey privateKey;

		[[nodiscard]] static RSAKeyPair Generate(KeySize _keySize = KeySize::RSA_2048);
	};

END_NS
