//
// Created by nebula on 24. 11. 10.
//

#include <gtest/gtest.h>
#include "Cryptography/AES/AES.h"



namespace crypto = ne::crypto;

static ne::string_t fromHex(const ne::string_t& _hex)
{
	ne::string_t _bytes;
	for (size_t i = 0; i + 1 < _hex.size(); i += 2)
	{
		auto h = [](ne::char_t c) -> ne::byte_t
		{
			if (c >= '0' && c <= '9') return static_cast<ne::byte_t>(c - '0');
			if (c >= 'a' && c <= 'f') return static_cast<ne::byte_t>(c - 'a' + 10);
			return static_cast<ne::byte_t>(c - 'A' + 10);
		};
		_bytes += static_cast<ne::char_t>((h(_hex[i]) << 4) | h(_hex[i + 1]));
	}
	return _bytes;
}

static ne::string_t toHex(const ne::string_t& _bytes)
{
	static constexpr ne::char_t _hex[] = "0123456789abcdef";
	ne::string_t result;
	for (ne::byte_t b : _bytes)
	{
		result += _hex[b >> 4];
		result += _hex[b & 0xF];
	}
	return result;
}

// NIST FIPS 197 Appendix B — AES-128 ECB single block
TEST(AESTest, EncryptECB_AES128_NIST)
{
	const ne::string_t key = fromHex("2b7e151628aed2a6abf7158809cf4f3c");
	const ne::string_t plaintext = fromHex("3243f6a8885a308d313198a2e0370734");

	ne::string_t ct = crypto::AES::Create(crypto::AES::Type::AES_128, key).EncryptECB(ne::string_t(plaintext));

	EXPECT_EQ(toHex(ct).substr(0, 32), "3925841d02dc09fbdc118597196a0b32");
}

// NIST SP 800-38A F.2.1 — AES-128 ECB round-trip
TEST(AESTest, RoundTrip_ECB_AES128)
{
	const ne::string_t key = fromHex("2b7e151628aed2a6abf7158809cf4f3c");
	const ne::string_t msg = "Hello, AES-128!";

	const auto aes = crypto::AES::Create(crypto::AES::Type::AES_128, key);
	ne::string_t ct = aes.EncryptECB(ne::string_t(msg));
	ne::string_t pt = aes.DecryptECB(ne::string_t(ct));

	EXPECT_EQ(pt, msg);
}

TEST(AESTest, RoundTrip_ECB_AES256)
{
	const ne::string_t key = fromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
	const ne::string_t msg = "AES-256 ECB test message!";

	const auto aes = crypto::AES::Create(crypto::AES::Type::AES_256, key);
	ne::string_t ct = aes.EncryptECB(ne::string_t(msg));
	ne::string_t pt = aes.DecryptECB(ne::string_t(ct));

	EXPECT_EQ(pt, msg);
}

TEST(AESTest, RoundTrip_CBC_AES128)
{
	const ne::string_t key = fromHex("2b7e151628aed2a6abf7158809cf4f3c");
	const ne::string_t iv = fromHex("000102030405060708090a0b0c0d0e0f");
	const ne::string_t msg = "CBC mode with PKCS7 padding test.";

	const auto aes = crypto::AES::Create(crypto::AES::Type::AES_128, key);
	ne::string_t ct = aes.EncryptCBC(ne::string_t(iv), ne::string_t(msg));
	ne::string_t pt = aes.DecryptCBC(ne::string_t(iv), ne::string_t(ct));

	EXPECT_EQ(pt, msg);
}

// NIST SP 800-38A F.2.1 — first CBC-128 block
TEST(AESTest, EncryptCBC_AES128_NIST_FirstBlock)
{
	const ne::string_t key = fromHex("2b7e151628aed2a6abf7158809cf4f3c");
	const ne::string_t iv = fromHex("000102030405060708090a0b0c0d0e0f");
	const ne::string_t pt = fromHex("6bc1bee22e409f96e93d7e117393172a");

	ne::string_t ct = crypto::AES::Create(crypto::AES::Type::AES_128, key).EncryptCBC(ne::string_t(iv), ne::string_t(pt));

	EXPECT_EQ(toHex(ct).substr(0, 32), "7649abac8119b246cee98e9b12e9197d");
}

TEST(AESTest, RoundTrip_CBC_AES256_LongMessage)
{
	const ne::string_t key = fromHex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
	const ne::string_t iv = fromHex("000102030405060708090a0b0c0d0e0f");
	const ne::string_t msg = "This is a longer message that spans multiple AES blocks for testing purposes.";

	const auto aes = crypto::AES::Create(crypto::AES::Type::AES_256, key);
	ne::string_t ct = aes.EncryptCBC(ne::string_t(iv), ne::string_t(msg));
	ne::string_t pt = aes.DecryptCBC(ne::string_t(iv), ne::string_t(ct));

	EXPECT_EQ(pt, msg);
}
