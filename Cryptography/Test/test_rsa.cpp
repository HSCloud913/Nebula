//
// Created by nebula on 24. 11. 10.
//

#include <gtest/gtest.h>
#include "Cryptography/RSA/RSA.h"
#include "Cryptography/Math/BigInt.h"



namespace crypto = ne::crypto;

TEST(RSATest, BigIntBasicArithmetic)
{
	using BI = crypto::BigInt;
	EXPECT_EQ((BI(12u) + BI(34u)).ToHex(), "2e");
	EXPECT_EQ((BI(100u) - BI(37u)).ToHex(), "3f");
	EXPECT_EQ((BI(12u) * BI(34u)).ToHex(), "198");
	EXPECT_EQ((BI(100u) / BI(7u)).ToHex(), "e");
	EXPECT_EQ((BI(100u) % BI(7u)).ToHex(), "2");
}

TEST(RSATest, BigIntModPow)
{
	using BI = crypto::BigInt;
	EXPECT_EQ(BI::ModPow(BI(2u), BI(10u), BI(1000u)).ToHex(), "18");
	EXPECT_EQ(BI::ModPow(BI(5u), BI(6u), BI(7u)).ToHex(), "1");
}

TEST(RSATest, BigIntModInverse)
{
	using BI = crypto::BigInt;
	EXPECT_EQ(BI::ModInverse(BI(3u), BI(7u)).ToHex(), "5");
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
	auto kp = crypto::RSAKeyPair::Generate(crypto::RSAKeyPair::KeySize::RSA_512);
	const ne::string_t msg = "Hello RSA";

	ne::string_t ct = kp.publicKey.Encrypt(ne::string_t(msg));
	ne::string_t pt = kp.privateKey.Decrypt(ne::string_t(ct));

	EXPECT_EQ(pt, msg);
}

TEST(RSATest, DISABLED_RoundTrip_RSA1024)
{
	auto kp = crypto::RSAKeyPair::Generate(crypto::RSAKeyPair::KeySize::RSA_1024);
	const ne::string_t msg = "Nebula RSA-1024 test message.";

	ne::string_t ct = kp.publicKey.Encrypt(ne::string_t(msg));
	ne::string_t pt = kp.privateKey.Decrypt(ne::string_t(ct));

	EXPECT_EQ(pt, msg);
}

TEST(RSATest, KeyPairHasExpectedExponent)
{
	auto kp = crypto::RSAKeyPair::Generate(crypto::RSAKeyPair::KeySize::RSA_512);
	EXPECT_EQ(kp.publicKey.e, "10001");
}

TEST(RSATest, EncryptDifferentEachTime)
{
	auto kp = crypto::RSAKeyPair::Generate(crypto::RSAKeyPair::KeySize::RSA_512);
	const ne::string_t msg = "test";

	ne::string_t ct1 = kp.publicKey.Encrypt(ne::string_t(msg));
	ne::string_t ct2 = kp.publicKey.Encrypt(ne::string_t(msg));

	EXPECT_NE(ct1, ct2);
}
