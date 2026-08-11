//
// Created by hscloud on 26. 8. 6.
//
// HKDF(RFC 5869) / PBKDF2(RFC 2898, 벡터는 RFC 6070) 표준 테스트 벡터 검증.

#include <gtest/gtest.h>
#include "Cryptography/Kdf.h"
#include "Util/Hex.h"



namespace crypto = ne::crypto;

namespace
{
	ne::string_t FromHex(const ne::string_view_t _hex) { return ne::util::Hex::Decode(_hex).value_or(ne::string_t{}); }
	ne::string_t ToHex(const ne::string_view_t _bytes) { return ne::util::Hex::Encode(_bytes); }
}



// ───────────────────────── HKDF (RFC 5869) ─────────────────────────

// Test Case 1: SHA-256, 기본 입력
TEST(KdfTest, HkdfRfc5869Case1)
{
	const ne::string_t ikm = FromHex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
	const ne::string_t salt = FromHex("000102030405060708090a0b0c");
	const ne::string_t info = FromHex("f0f1f2f3f4f5f6f7f8f9");

	auto prk = crypto::HkdfExtract(crypto::HashType::SHA2_256, ikm, salt);
	ASSERT_TRUE(prk.IsOk()) << prk.Error().What();
	EXPECT_EQ(ToHex(prk.Value()), "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5");

	auto okm = crypto::HkdfExpand(crypto::HashType::SHA2_256, prk.Value(), info, 42);
	ASSERT_TRUE(okm.IsOk()) << okm.Error().What();
	EXPECT_EQ(ToHex(okm.Value()), "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");

	// 통합 Hkdf() 는 Extract+Expand 와 같은 결과를 내야 한다.
	auto combined = crypto::Hkdf(crypto::HashType::SHA2_256, ikm, salt, info, 42);
	ASSERT_TRUE(combined.IsOk());
	EXPECT_EQ(combined.Value(), okm.Value());
}

// Test Case 3: SHA-256, salt/info 없음 — 빈 salt 처리 경로 검증
TEST(KdfTest, HkdfRfc5869Case3)
{
	const ne::string_t ikm = FromHex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");

	auto prk = crypto::HkdfExtract(crypto::HashType::SHA2_256, ikm);
	ASSERT_TRUE(prk.IsOk());
	EXPECT_EQ(ToHex(prk.Value()), "19ef24a32c717b167f33a91d6f648bdf96596776afdb6377ac434c1c293ccb04");

	auto okm = crypto::HkdfExpand(crypto::HashType::SHA2_256, prk.Value(), "", 42);
	ASSERT_TRUE(okm.IsOk());
	EXPECT_EQ(ToHex(okm.Value()), "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8");
}

// Test Case 4: SHA-1
TEST(KdfTest, HkdfRfc5869Case4Sha1)
{
	const ne::string_t ikm = FromHex("0b0b0b0b0b0b0b0b0b0b0b");
	const ne::string_t salt = FromHex("000102030405060708090a0b0c");
	const ne::string_t info = FromHex("f0f1f2f3f4f5f6f7f8f9");

	auto okm = crypto::Hkdf(crypto::HashType::SHA1, ikm, salt, info, 42);
	ASSERT_TRUE(okm.IsOk()) << okm.Error().What();
	EXPECT_EQ(ToHex(okm.Value()), "085a01ea1b10f36933068b56efa5ad81a4f14b822f5b091568a9cdd4f155fda2c22e422478d305f3f896");
}

// 서로 다른 info 는 독립적인 키를 만든다 — 키 재사용을 막는 유일한 장치.
TEST(KdfTest, HkdfInfoSeparatesKeys)
{
	const ne::string_t master(32, 'k');

	auto encryption = crypto::HkdfExpand(crypto::HashType::SHA2_256, master, "app-enc", 32);
	auto mac = crypto::HkdfExpand(crypto::HashType::SHA2_256, master, "app-mac", 32);
	ASSERT_TRUE(encryption.IsOk());
	ASSERT_TRUE(mac.IsOk());

	EXPECT_NE(encryption.Value(), mac.Value());
	EXPECT_NE(encryption.Value(), master);
	EXPECT_EQ(encryption.Value().size(), 32u);
}

// 요청 길이가 여러 블록에 걸쳐도(해시 길이 초과) 앞부분은 짧은 요청과 동일해야 한다.
TEST(KdfTest, HkdfLongOutputIsPrefixConsistent)
{
	const ne::string_t prk(32, 'p');

	auto shortOkm = crypto::HkdfExpand(crypto::HashType::SHA2_256, prk, "info", 32);
	auto longOkm = crypto::HkdfExpand(crypto::HashType::SHA2_256, prk, "info", 100);
	ASSERT_TRUE(shortOkm.IsOk());
	ASSERT_TRUE(longOkm.IsOk());

	EXPECT_EQ(longOkm.Value().size(), 100u);
	EXPECT_EQ(longOkm.Value().substr(0, 32), shortOkm.Value());
}

TEST(KdfTest, HkdfRejectsInvalidRequests)
{
	const ne::string_t prk(32, 'p');

	EXPECT_TRUE(crypto::HkdfExpand(crypto::HashType::SHA2_256, prk, "info", 0).IsError());          // 길이 0
	EXPECT_TRUE(crypto::HkdfExpand(crypto::HashType::SHA2_256, prk, "info", 255 * 32 + 1).IsError()); // 255*HashLen 초과

	// CRC32 는 MAC 에 쓸 수 없으므로 KDF 에도 쓸 수 없다.
	auto rejected = crypto::HkdfExtract(crypto::HashType::CRC32, "ikm", "salt");
	ASSERT_TRUE(rejected.IsError());
	EXPECT_EQ(rejected.Error().Kind(), crypto::CryptoErrorKind::INVALID_INPUT);
}



// ───────────────────────── PBKDF2 (RFC 6070 벡터) ─────────────────────────

TEST(KdfTest, Pbkdf2Rfc6070Vectors)
{
	auto one = crypto::Pbkdf2(crypto::HashType::SHA1, "password", "salt", 1, 20);
	ASSERT_TRUE(one.IsOk()) << one.Error().What();
	EXPECT_EQ(ToHex(one.Value()), "0c60c80f961f0e71f3a9b524af6012062fe037a6");

	auto two = crypto::Pbkdf2(crypto::HashType::SHA1, "password", "salt", 2, 20);
	ASSERT_TRUE(two.IsOk());
	EXPECT_EQ(ToHex(two.Value()), "ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957");

	// dkLen 이 해시 출력보다 길어 여러 블록을 이어 붙이는 경로.
	auto multiBlock = crypto::Pbkdf2(crypto::HashType::SHA1, "passwordPASSWORDpassword", "saltSALTsaltSALTsaltSALTsaltSALTsalt", 4096, 25);
	ASSERT_TRUE(multiBlock.IsOk());
	EXPECT_EQ(ToHex(multiBlock.Value()), "3d2eec4fe41c849b80c8d83662c0e44a8b291a964cf2f07038");
}

TEST(KdfTest, Pbkdf2Sha256IsUsableForAesKey)
{
	auto key = crypto::Pbkdf2(crypto::HashType::SHA2_256, "correct horse battery staple", "per-user-random-salt", 1000, 32);
	ASSERT_TRUE(key.IsOk()) << key.Error().What();
	EXPECT_EQ(key.Value().size(), 32u); // AES-256 키 길이

	// salt 가 다르면 완전히 다른 키가 나온다(레인보우 테이블 방어의 핵심).
	auto other = crypto::Pbkdf2(crypto::HashType::SHA2_256, "correct horse battery staple", "different-salt", 1000, 32);
	ASSERT_TRUE(other.IsOk());
	EXPECT_NE(key.Value(), other.Value());

	// 반복 횟수가 다르면 결과도 다르다.
	auto fewer = crypto::Pbkdf2(crypto::HashType::SHA2_256, "correct horse battery staple", "per-user-random-salt", 999, 32);
	ASSERT_TRUE(fewer.IsOk());
	EXPECT_NE(key.Value(), fewer.Value());
}

TEST(KdfTest, Pbkdf2RejectsInvalidRequests)
{
	EXPECT_TRUE(crypto::Pbkdf2(crypto::HashType::SHA2_256, "pw", "salt", 0, 32).IsError());  // 반복 0
	EXPECT_TRUE(crypto::Pbkdf2(crypto::HashType::SHA2_256, "pw", "salt", 100, 0).IsError()); // 길이 0
	EXPECT_TRUE(crypto::Pbkdf2(crypto::HashType::CRC32, "pw", "salt", 100, 32).IsError());   // 비암호 해시
}
