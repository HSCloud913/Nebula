//
// Created by nebula on 24. 11. 10.
//
// AES 저수준 primitive 검증. ECB 는 공개 표면에서 제거됐으므로 표준 벡터 검증만 Internal 로 한다.
// 인증이 포함된 실사용 경로는 test_seal.cpp 를 참고.

#include <gtest/gtest.h>
#include "Cryptography/AES.h"
#include "Cryptography/Internal/Ecb.h"
#include "Util/Hex.h"



namespace crypto = ne::crypto;

namespace
{
	ne::string_t FromHex(const ne::string_view_t _hex) { return ne::util::Hex::Decode(_hex).value_or(ne::string_t{}); }
	ne::string_t ToHex(const ne::string_view_t _bytes) { return ne::util::Hex::Encode(_bytes); }

	crypto::AES MakeAes(const crypto::AES::Type _type, const ne::string_view_t _key)
	{
		auto aes = crypto::AES::Create(_type, _key);
		EXPECT_TRUE(aes.IsOk()) << aes.Error().What();

		return std::move(aes.Value());
	}
}



// ───────────────────────── 키 길이 검증 ─────────────────────────

TEST(AESTest, CreateRejectsWrongKeyLength)
{
	const ne::string_t key16 = FromHex("2b7e151628aed2a6abf7158809cf4f3c");

	EXPECT_TRUE(crypto::AES::Create(crypto::AES::Type::AES_128, key16).IsOk());

	// AES_256 은 32바이트를 요구한다 — 짧은 키를 조용히 받아들이면 안 된다.
	auto mismatched = crypto::AES::Create(crypto::AES::Type::AES_256, key16);
	ASSERT_TRUE(mismatched.IsError());
	EXPECT_EQ(mismatched.Error().Kind(), crypto::CryptoErrorKind::INVALID_KEY_LENGTH);

	EXPECT_TRUE(crypto::AES::Create(crypto::AES::Type::AES_128, "").IsError());
}



// ───────────────────────── ECB (표준 벡터 전용, Internal) ─────────────────────────

// NIST FIPS 197 Appendix B — AES-128 단일 블록
TEST(AESTest, EncryptEcb_AES128_NIST)
{
	const ne::string_t key = FromHex("2b7e151628aed2a6abf7158809cf4f3c");
	const ne::string_t plaintext = FromHex("3243f6a8885a308d313198a2e0370734");

	auto ct = crypto::internal::EncryptEcb(crypto::AES::Type::AES_128, key, plaintext);
	ASSERT_TRUE(ct.IsOk()) << ct.Error().What();

	EXPECT_EQ(ToHex(ct.Value()).substr(0, 32), "3925841d02dc09fbdc118597196a0b32");
}

TEST(AESTest, RoundTrip_Ecb_AES128)
{
	const ne::string_t key = FromHex("2b7e151628aed2a6abf7158809cf4f3c");
	const ne::string_t msg = "Hello, AES-128!";

	auto ct = crypto::internal::EncryptEcb(crypto::AES::Type::AES_128, key, msg);
	ASSERT_TRUE(ct.IsOk());

	auto pt = crypto::internal::DecryptEcb(crypto::AES::Type::AES_128, key, ct.Value());
	ASSERT_TRUE(pt.IsOk()) << pt.Error().What();
	EXPECT_EQ(pt.Value(), msg);
}

TEST(AESTest, RoundTrip_Ecb_AES256)
{
	const ne::string_t key = FromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
	const ne::string_t msg = "AES-256 ECB test message!";

	auto ct = crypto::internal::EncryptEcb(crypto::AES::Type::AES_256, key, msg);
	ASSERT_TRUE(ct.IsOk());

	auto pt = crypto::internal::DecryptEcb(crypto::AES::Type::AES_256, key, ct.Value());
	ASSERT_TRUE(pt.IsOk());
	EXPECT_EQ(pt.Value(), msg);
}



// ───────────────────────── CBC ─────────────────────────

TEST(AESTest, RoundTrip_CBC_AES128)
{
	const ne::string_t key = FromHex("2b7e151628aed2a6abf7158809cf4f3c");
	const ne::string_t iv = FromHex("000102030405060708090a0b0c0d0e0f");
	const ne::string_t msg = "CBC mode with PKCS7 padding test.";

	const crypto::AES aes = MakeAes(crypto::AES::Type::AES_128, key);

	auto ct = aes.EncryptCBC(iv, msg);
	ASSERT_TRUE(ct.IsOk()) << ct.Error().What();

	auto pt = aes.DecryptCBC(iv, ct.Value());
	ASSERT_TRUE(pt.IsOk()) << pt.Error().What();
	EXPECT_EQ(pt.Value(), msg);
}

// NIST SP 800-38A F.2.1 — CBC-128 첫 블록
TEST(AESTest, EncryptCBC_AES128_NIST_FirstBlock)
{
	const ne::string_t key = FromHex("2b7e151628aed2a6abf7158809cf4f3c");
	const ne::string_t iv = FromHex("000102030405060708090a0b0c0d0e0f");
	const ne::string_t plaintext = FromHex("6bc1bee22e409f96e93d7e117393172a");

	auto ct = MakeAes(crypto::AES::Type::AES_128, key).EncryptCBC(iv, plaintext);
	ASSERT_TRUE(ct.IsOk());

	EXPECT_EQ(ToHex(ct.Value()).substr(0, 32), "7649abac8119b246cee98e9b12e9197d");
}

TEST(AESTest, RoundTrip_CBC_AES256_LongMessage)
{
	const ne::string_t key = FromHex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
	const ne::string_t iv = FromHex("000102030405060708090a0b0c0d0e0f");
	const ne::string_t msg = "This is a longer message that spans multiple AES blocks for testing purposes.";

	const crypto::AES aes = MakeAes(crypto::AES::Type::AES_256, key);

	auto ct = aes.EncryptCBC(iv, msg);
	ASSERT_TRUE(ct.IsOk());

	auto pt = aes.DecryptCBC(iv, ct.Value());
	ASSERT_TRUE(pt.IsOk());
	EXPECT_EQ(pt.Value(), msg);
}

// IV 자동 생성 오버로드: 호출마다 새 IV → 같은 평문도 다른 암호문.
TEST(AESTest, EncryptCBC_GeneratesFreshIv)
{
	const crypto::AES aes = MakeAes(crypto::AES::Type::AES_256, FromHex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"));

	auto first = aes.EncryptCBC("same plaintext");
	auto second = aes.EncryptCBC("same plaintext");
	ASSERT_TRUE(first.IsOk()) << first.Error().What();
	ASSERT_TRUE(second.IsOk());

	EXPECT_EQ(first.Value().iv.size(), crypto::AES::BlockSize);
	EXPECT_NE(first.Value().iv, second.Value().iv);
	EXPECT_NE(first.Value().ciphertext, second.Value().ciphertext);

	auto pt = aes.DecryptCBC(first.Value().iv, first.Value().ciphertext);
	ASSERT_TRUE(pt.IsOk());
	EXPECT_EQ(pt.Value(), "same plaintext");
}



// ───────────────────────── 입력 검증 (과거 조용히 통과했던 경로) ─────────────────────────

TEST(AESTest, RejectsBadIvLength)
{
	const crypto::AES aes = MakeAes(crypto::AES::Type::AES_128, FromHex("2b7e151628aed2a6abf7158809cf4f3c"));

	auto shortIv = aes.EncryptCBC(FromHex("0001020304"), "payload");
	ASSERT_TRUE(shortIv.IsError());
	EXPECT_EQ(shortIv.Error().Kind(), crypto::CryptoErrorKind::INVALID_INPUT);

	EXPECT_TRUE(aes.DecryptCBC(FromHex("0001020304"), FromHex("00112233445566778899aabbccddeeff")).IsError());
}

// 블록 정렬이 깨진 암호문은 실패해야 한다.
TEST(AESTest, RejectsUnalignedCiphertext)
{
	const crypto::AES aes = MakeAes(crypto::AES::Type::AES_128, FromHex("2b7e151628aed2a6abf7158809cf4f3c"));
	const ne::string_t iv = FromHex("000102030405060708090a0b0c0d0e0f");

	auto unaligned = aes.DecryptCBC(iv, FromHex("00112233"));
	ASSERT_TRUE(unaligned.IsError());
	EXPECT_EQ(unaligned.Error().Kind(), crypto::CryptoErrorKind::INVALID_INPUT);

	EXPECT_TRUE(aes.DecryptCBC(iv, "").IsError());
}

// ───────────────────────── 한 줄 진입점 ─────────────────────────

TEST(AESTest, OneLinerRoundTrip)
{
	const ne::string_t key = FromHex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");

	auto sealed = crypto::AesEncrypt(crypto::AES::Type::AES_256, key, "one-liner payload");
	ASSERT_TRUE(sealed.IsOk()) << sealed.Error().What();

	auto opened = crypto::AesDecrypt(crypto::AES::Type::AES_256, key, sealed.Value().iv, sealed.Value().ciphertext);
	ASSERT_TRUE(opened.IsOk()) << opened.Error().What();
	EXPECT_EQ(opened.Value(), "one-liner payload");

	// 객체 경로와 동일한 결과여야 한다(같은 IV 를 주면).
	const crypto::AES aes = MakeAes(crypto::AES::Type::AES_256, key);
	auto viaObject = aes.EncryptCBC(sealed.Value().iv, "one-liner payload");
	ASSERT_TRUE(viaObject.IsOk());
	EXPECT_EQ(viaObject.Value(), sealed.Value().ciphertext);
}

TEST(AESTest, OneLinerPropagatesKeyLengthError)
{
	auto badKey = crypto::AesEncrypt(crypto::AES::Type::AES_256, FromHex("2b7e151628aed2a6abf7158809cf4f3c"), "payload");
	ASSERT_TRUE(badKey.IsError());
	EXPECT_EQ(badKey.Error().Kind(), crypto::CryptoErrorKind::INVALID_KEY_LENGTH);

	EXPECT_TRUE(crypto::AesDecrypt(crypto::AES::Type::AES_128, "short", FromHex("000102030405060708090a0b0c0d0e0f"), FromHex("00112233445566778899aabbccddeeff")).IsError());
}



// 패딩이 깨진 복호는 실패로 보고해야 한다 — 과거에는 쓰레기를 성공처럼 돌려줬다(회귀 방지).
TEST(AESTest, MalformedPaddingIsReported)
{
	const ne::string_t key = FromHex("2b7e151628aed2a6abf7158809cf4f3c");
	const ne::string_t iv = FromHex("000102030405060708090a0b0c0d0e0f");
	const crypto::AES aes = MakeAes(crypto::AES::Type::AES_128, key);

	auto ct = aes.EncryptCBC(iv, "payload");
	ASSERT_TRUE(ct.IsOk());

	// 마지막 블록을 뒤집으면 패딩 바이트가 깨진다.
	ne::string_t corrupted = ct.Value();
	corrupted[corrupted.size() - 1] = static_cast<ne::char_t>(corrupted.back() ^ 0xFF);

	auto pt = aes.DecryptCBC(iv, corrupted);
	ASSERT_TRUE(pt.IsError()) << "깨진 패딩이 성공으로 보고되었다";
	EXPECT_EQ(pt.Error().Kind(), crypto::CryptoErrorKind::MALFORMED_PADDING);
}
