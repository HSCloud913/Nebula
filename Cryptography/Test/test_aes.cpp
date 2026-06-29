//
// Created by nebula on 24. 11. 10.
//

#include <gtest/gtest.h>
#include "AES/AES.h"
#include "Base64/Base64.h"



namespace crypto = ne::cryptography;

// Helper: hex string → binary string
static ne::string_t fromHex(const ne::string_t& hex)
{
	ne::string_t bytes;
	for (size_t i = 0; i + 1 < hex.size(); i += 2)
	{
		auto h = [](ne::char_t c) -> ne::byte_t {
			if (c >= '0' && c <= '9') return static_cast<ne::byte_t>(c - '0');
			if (c >= 'a' && c <= 'f') return static_cast<ne::byte_t>(c - 'a' + 10);
			return static_cast<ne::byte_t>(c - 'A' + 10);
		};
		bytes += static_cast<ne::char_t>((h(hex[i]) << 4) | h(hex[i + 1]));
	}
	return bytes;
}

// Helper: binary string → lowercase hex string
static ne::string_t toHex(const ne::string_t& bytes)
{
	static constexpr ne::char_t hex[] = "0123456789abcdef";
	ne::string_t result;
	for (ne::byte_t b : bytes)
	{
		result += hex[b >> 4];
		result += hex[b & 0xF];
	}
	return result;
}

// NIST FIPS 197 Appendix B — AES-128 ECB single block
TEST(AESTest, EncryptECB_AES128_NIST)
{
	const ne::string_t key       = fromHex("2b7e151628aed2a6abf7158809cf4f3c");
	const ne::string_t plaintext = fromHex("3243f6a8885a308d313198a2e0370734");

	// Single 16-byte block: no PKCS#7 extra block, just one padded block output
	// After PKCS#7: plaintext (16 bytes) + 16 bytes of 0x10 = 32 bytes ciphertext
	// First block must equal the NIST expected ciphertext
	ne::string_t ct = crypto::AES::EncryptECB(crypto::AES::KeyType::AES128,
		ne::string_t(key), ne::string_t(plaintext));

	EXPECT_EQ(toHex(ct).substr(0, 32), "3925841d02dc09fbdc118597196a0b32");
}

// NIST SP 800-38A F.2.1 — AES-128 ECB round-trip
TEST(AESTest, RoundTrip_ECB_AES128)
{
	const ne::string_t key = fromHex("2b7e151628aed2a6abf7158809cf4f3c");
	const ne::string_t msg = "Hello, AES-128!";

	ne::string_t ct = crypto::AES::EncryptECB(crypto::AES::KeyType::AES128,
		ne::string_t(key), ne::string_t(msg));
	ne::string_t pt = crypto::AES::DecryptECB(crypto::AES::KeyType::AES128,
		ne::string_t(key), ne::string_t(ct));

	EXPECT_EQ(pt, msg);
}

TEST(AESTest, RoundTrip_ECB_AES256)
{
	const ne::string_t key = fromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
	const ne::string_t msg = "AES-256 ECB test message!";

	ne::string_t ct = crypto::AES::EncryptECB(crypto::AES::KeyType::AES256,
		ne::string_t(key), ne::string_t(msg));
	ne::string_t pt = crypto::AES::DecryptECB(crypto::AES::KeyType::AES256,
		ne::string_t(key), ne::string_t(ct));

	EXPECT_EQ(pt, msg);
}

TEST(AESTest, RoundTrip_CBC_AES128)
{
	const ne::string_t key = fromHex("2b7e151628aed2a6abf7158809cf4f3c");
	const ne::string_t iv  = fromHex("000102030405060708090a0b0c0d0e0f");
	const ne::string_t msg = "CBC mode with PKCS7 padding test.";

	ne::string_t ct = crypto::AES::EncryptCBC(crypto::AES::KeyType::AES128,
		ne::string_t(key), ne::string_t(iv), ne::string_t(msg));
	ne::string_t pt = crypto::AES::DecryptCBC(crypto::AES::KeyType::AES128,
		ne::string_t(key), ne::string_t(iv), ne::string_t(ct));

	EXPECT_EQ(pt, msg);
}

// NIST SP 800-38A F.2.1 — first CBC-128 block
TEST(AESTest, EncryptCBC_AES128_NIST_FirstBlock)
{
	const ne::string_t key = fromHex("2b7e151628aed2a6abf7158809cf4f3c");
	const ne::string_t iv  = fromHex("000102030405060708090a0b0c0d0e0f");
	const ne::string_t pt  = fromHex("6bc1bee22e409f96e93d7e117393172a");

	ne::string_t ct = crypto::AES::EncryptCBC(crypto::AES::KeyType::AES128,
		ne::string_t(key), ne::string_t(iv), ne::string_t(pt));

	// First 16 bytes must match NIST expected
	EXPECT_EQ(toHex(ct).substr(0, 32), "7649abac8119b246cee98e9b12e9197d");
}

TEST(AESTest, RoundTrip_CBC_AES256_LongMessage)
{
	const ne::string_t key = fromHex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
	const ne::string_t iv  = fromHex("000102030405060708090a0b0c0d0e0f");
	const ne::string_t msg = "This is a longer message that spans multiple AES blocks for testing purposes.";

	ne::string_t ct = crypto::AES::EncryptCBC(crypto::AES::KeyType::AES256,
		ne::string_t(key), ne::string_t(iv), ne::string_t(msg));
	ne::string_t pt = crypto::AES::DecryptCBC(crypto::AES::KeyType::AES256,
		ne::string_t(key), ne::string_t(iv), ne::string_t(ct));

	EXPECT_EQ(pt, msg);
}
