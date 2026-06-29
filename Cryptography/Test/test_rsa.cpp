//
// Created by nebula on 24. 11. 10.
//

#include <gtest/gtest.h>
#include "RSA/RSA.h"
#include "RSA/BigInt.h"



namespace crypto = ne::cryptography;

TEST(RSATest, BigIntBasicArithmetic)
{
	using BI = crypto::BigInt;
	EXPECT_EQ((BI(12u) + BI(34u)).ToHex(), "2e");    // 46
	EXPECT_EQ((BI(100u) - BI(37u)).ToHex(), "3f");   // 63
	EXPECT_EQ((BI(12u) * BI(34u)).ToHex(), "198");   // 408
	EXPECT_EQ((BI(100u) / BI(7u)).ToHex(), "e");     // 14
	EXPECT_EQ((BI(100u) % BI(7u)).ToHex(), "2");     // 2
}

TEST(RSATest, BigIntModPow)
{
	using BI = crypto::BigInt;
	// 2^10 mod 1000 = 24
	EXPECT_EQ(BI::ModPow(BI(2u), BI(10u), BI(1000u)).ToHex(), "18"); // 24

	// Fermat little theorem: a^(p-1) ≡ 1 (mod p) for prime p
	// 5^6 mod 7 = 1
	EXPECT_EQ(BI::ModPow(BI(5u), BI(6u), BI(7u)).ToHex(), "1");
}

TEST(RSATest, BigIntModInverse)
{
	using BI = crypto::BigInt;
	// 3 * 3 mod 7 = 9 mod 7 = 2, but inverse of 3 mod 7 = 5 (3*5=15 ≡ 1 mod 7)
	EXPECT_EQ(BI::ModInverse(BI(3u), BI(7u)).ToHex(), "5");
	// inverse of 2 mod 5 = 3 (2*3=6 ≡ 1 mod 5)
	EXPECT_EQ(BI::ModInverse(BI(2u), BI(5u)).ToHex(), "3");
}

TEST(RSATest, BigIntPrimality)
{
	using BI = crypto::BigInt;
	EXPECT_TRUE(BI(2u).IsProbablyPrime());
	EXPECT_TRUE(BI(3u).IsProbablyPrime());
	EXPECT_TRUE(BI(17u).IsProbablyPrime());
	EXPECT_TRUE(BI(65537u).IsProbablyPrime());
	EXPECT_FALSE(BI(4u).IsProbablyPrime());
	EXPECT_FALSE(BI(100u).IsProbablyPrime());
}

TEST(RSATest, RoundTrip_RSA512)
{
	auto kp = crypto::RSA::GenerateKeyPair(crypto::RSA::KeySize::RSA512);
	const ne::string_t msg = "Hello RSA";

	ne::string_t ct = crypto::RSA::Encrypt(kp.publicKey, ne::string_t(msg));
	ne::string_t pt = crypto::RSA::Decrypt(kp.privateKey, ne::string_t(ct));

	EXPECT_EQ(pt, msg);
}

TEST(RSATest, DISABLED_RoundTrip_RSA1024)
{
	auto kp = crypto::RSA::GenerateKeyPair(crypto::RSA::KeySize::RSA1024);
	const ne::string_t msg = "Nebula RSA-1024 test message.";

	ne::string_t ct = crypto::RSA::Encrypt(kp.publicKey, ne::string_t(msg));
	ne::string_t pt = crypto::RSA::Decrypt(kp.privateKey, ne::string_t(ct));

	EXPECT_EQ(pt, msg);
}

TEST(RSATest, KeyPairHasExpectedExponent)
{
	auto kp = crypto::RSA::GenerateKeyPair(crypto::RSA::KeySize::RSA512);
	// Public exponent should be 65537 = 0x10001
	EXPECT_EQ(kp.publicKey.e, "10001");
}

TEST(RSATest, EncryptDifferentEachTime)
{
	// Probabilistic encryption: same message encrypted twice should differ
	auto kp = crypto::RSA::GenerateKeyPair(crypto::RSA::KeySize::RSA512);
	const ne::string_t msg = "test";

	ne::string_t ct1 = crypto::RSA::Encrypt(kp.publicKey, ne::string_t(msg));
	ne::string_t ct2 = crypto::RSA::Encrypt(kp.publicKey, ne::string_t(msg));

	// Due to random padding, outputs should differ
	EXPECT_NE(ct1, ct2);
}
