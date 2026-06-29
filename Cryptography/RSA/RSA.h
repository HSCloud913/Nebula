#ifndef NEBULA_RSA_H
#define NEBULA_RSA_H

#include "Type.h"

BEGIN_NS(ne::cryptography)

struct RSAPublicKey
{
	ne::string_t n; // modulus (hex)
	ne::string_t e; // public exponent (hex)
};

struct RSAPrivateKey
{
	ne::string_t n; // modulus (hex)
	ne::string_t d; // private exponent (hex)
};

struct RSAKeyPair
{
	RSAPublicKey  publicKey;
	RSAPrivateKey privateKey;
};

class RSA final
{
	NEBULA_NON_COPYABLE_MOVABLE(RSA)

private:
	explicit RSA() = default;
	~RSA() = default;

public:
	enum class KeySize { RSA512 = 512, RSA1024 = 1024, RSA2048 = 2048 };

	static RSAKeyPair GenerateKeyPair(KeySize _keySize = KeySize::RSA2048);

	// PKCS#1 v1.5 padding — output is raw binary bytes
	static string_t Encrypt(const RSAPublicKey& _key, string_t&& _plaintext);
	static string_t Decrypt(const RSAPrivateKey& _key, string_t&& _ciphertext);
};

END_NS

#endif //NEBULA_RSA_H
