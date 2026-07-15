#pragma once
#include "Base/Type.h"

BEGIN_NS(ne::crypto)
	/** @brief RSA 공개키(n, e)로 평문을 암호화합니다. */
	struct RSAPublicKey
	{
		ne::string_t n;
		ne::string_t e;

		[[nodiscard]] ne::string_t Encrypt(ne::string_t&& _plainText) const;
	};

	/** @brief RSA 개인키(n, d)로 암호문을 복호화합니다. */
	struct RSAPrivateKey
	{
		ne::string_t n;
		ne::string_t d;

		[[nodiscard]] ne::string_t Decrypt(ne::string_t&& _cipherText) const;
	};

	/**
	 * @class RSAKeyPair
	 * @brief RSA 공개키/개인키 쌍을 생성하는 팩토리입니다.
	 */
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
