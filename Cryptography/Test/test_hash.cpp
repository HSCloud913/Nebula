//
// Created by nebula on 24. 11. 10.
//
// 해시/HMAC 공개 파사드 검증. 파일 픽스처는 저장소 내(Test/data)에 두고 경로는 CMake 가 주입한다.
// 증분 해시(Init/AddBuffer 반복) 계약은 HMAC 이 의존하므로 Internal 알고리즘으로 직접 검증한다.

#include <gtest/gtest.h>
#include <cstdio>
#include "Cryptography/Hash.h"
#include "Cryptography/HMAC.h"
#include "Cryptography/Internal/ConstantTime.h"
#include "Cryptography/Internal/Algorithm/SHA2.h"



namespace crypto = ne::crypto;

namespace
{
	// 파일 해시 픽스처 — 저장소 내(Cryptography/Test/data)에 두고 경로는 CMake 가 주입한다.
	// (과거처럼 gitignore 된 _bin 절대 경로에 의존하면 클린 클론/CI 에서 항상 실패한다.)
	ne::string_t TestFilePath() { return NEBULA_CRYPTO_TEST_DATA_DIR "/hash_test.txt"; }
}

// 모든 해시는 소문자 hex 를 반환한다(과거 CRC32 만 대문자였던 특례는 2026-08-03 에 제거).
TEST(HashTest, Crc32String)
{
	EXPECT_EQ(crypto::Hash(crypto::HashType::CRC32, "nebula_crypto_test"), "c7094b48");
}

TEST(HashTest, Crc32File)
{
	EXPECT_EQ(crypto::HashFile(crypto::HashType::CRC32, TestFilePath()), "45632f7c");
}

TEST(HashTest, Md5String)
{
	EXPECT_EQ(crypto::Hash(crypto::HashType::MD5, "nebula_crypto_test"), "0ef182ba075eff6de23c73d4e9a409a8");
}

TEST(HashTest, Md5File)
{
	EXPECT_EQ(crypto::HashFile(crypto::HashType::MD5, TestFilePath()), "0c844ec323b285191143c8481e2516cf");
}

TEST(HashTest, Sha1String)
{
	EXPECT_EQ(crypto::Hash(crypto::HashType::SHA1, "nebula_crypto_test"), "534b46800f03c633f58552905c3429c0f25fbb4b");
}

TEST(HashTest, Sha1File)
{
	EXPECT_EQ(crypto::HashFile(crypto::HashType::SHA1, TestFilePath()), "23189a05ce284ab5a01db409e82d29131bab266f");
}

TEST(HashTest, Sha2String)
{
	EXPECT_EQ(crypto::Hash(crypto::HashType::SHA2_224, "nebula_crypto_test"), "42b23380642dca8c745a7fefee33401b9a5f88282e74abc7779315a2");
	EXPECT_EQ(crypto::Hash(crypto::HashType::SHA2_256, "nebula_crypto_test"), "57e8c9b95dae156a76f6749d8ae381a7dedd00e36c4bab305d7fc96b08a1cafd");
	EXPECT_EQ(crypto::Hash(crypto::HashType::SHA2_384, "nebula_crypto_test"), "f92d240bcfa2e062246160716ef3c44d4619e02a035108534ae404da4b3c89411e1122afbf3320236cd0d42bc351ba98");
	EXPECT_EQ(crypto::Hash(crypto::HashType::SHA2_512, "nebula_crypto_test"), "458027ded8e8916b677e616f3565b2fbf5ce95b4c936af9ad11901af0393faec52278b42416b7a9e46a720c59acebcf7e7f7ed0ad382159a00c63864b920eb94");
}

TEST(HashTest, Sha2File)
{
	EXPECT_EQ(crypto::HashFile(crypto::HashType::SHA2_224, TestFilePath()), "cc3394f255e4800e1ab2766871390aa9007c6ba45a0ddbdf813b6c35");
	EXPECT_EQ(crypto::HashFile(crypto::HashType::SHA2_256, TestFilePath()), "39f7a637bbed6db0c0db3914fd2d0521c9d3edde5b4f698ffb4872ce0c925377");
	EXPECT_EQ(crypto::HashFile(crypto::HashType::SHA2_384, TestFilePath()), "e4f194aec15e2dee857fd847cd946336e623aef53bdcb6033aae7d2cdf29e1d8c4b928601ec57d45cea5666e68198cd1");
	EXPECT_EQ(crypto::HashFile(crypto::HashType::SHA2_512, TestFilePath()), "6ffae33bf773f4ff595e676b40f5a9ca1aa88c31c7450fbcbfe8358fbcc7be6b2c3f6b7e9aa0ecc976d3ae114fb7424d52b6008aab4c29510385d8bc54f81abd");
}

TEST(HashTest, Sha3String)
{
	EXPECT_EQ(crypto::Hash(crypto::HashType::SHA3_224, "nebula_crypto_test"), "d48305ca85f518bed9fe6dedb66c4f30cf1969fe9fba4d412d2e93c9");
	EXPECT_EQ(crypto::Hash(crypto::HashType::SHA3_256, "nebula_crypto_test"), "d79a9c8f568cdd7fcf062793dd894f957d11009050606907ea008518199bca7f");
	EXPECT_EQ(crypto::Hash(crypto::HashType::SHA3_384, "nebula_crypto_test"), "2d3fc82307d901de619b10957f0c906a05c60e3e822b97f9347e79ea6b9125d0c1fe22f6ea57a009f89b92b9a2d9c60e");
	EXPECT_EQ(crypto::Hash(crypto::HashType::SHA3_512, "nebula_crypto_test"), "db037c67c891b44886beeba579da9cd8ecd49ce39d25540c4eebd200e2596c253ded3641711c2bc7c5af1cf999017064cef89c20abc6e7514041a2445f9203d1");
}

TEST(HashTest, Sha3File)
{
	EXPECT_EQ(crypto::HashFile(crypto::HashType::SHA3_224, TestFilePath()), "6ef7cd5b40a10e4db064745cefea4e634f92d8703052a83fe57db2d1");
	EXPECT_EQ(crypto::HashFile(crypto::HashType::SHA3_256, TestFilePath()), "9eb633197ac9ac9946421ef0c662c4f7e57967021aa2b57401ab0763745d5fe0");
	EXPECT_EQ(crypto::HashFile(crypto::HashType::SHA3_384, TestFilePath()), "0bcca95fe73112184d56abce905b12d4dbcd9f6323eb060b9b695d1a143c4aa1e94aa8aae996ad2156b691a76b01740d");
	EXPECT_EQ(crypto::HashFile(crypto::HashType::SHA3_512, TestFilePath()), "63646280705f8478ff89341a1d493c82efc58d9d977be40202b0e636670e1ccec4f9e7ca1bc8d3943dbf440a5ef07a79c34d68535a35a3a8262d8cf875843abc");
}

// 존재하지 않는 파일은 빈 문자열(파사드 계약).
TEST(HashTest, FileNotFoundReturnsEmpty)
{
	EXPECT_EQ(crypto::HashFile(crypto::HashType::SHA2_256, "no_such_file.bin"), "");
}

// 증분 해시: 청크 분할 AddBuffer == 원샷 — HMAC 이 의존하는 계약.
TEST(HashTest, StreamingChunksMatchOneShot)
{
	crypto::internal::SHA2 sha(crypto::internal::SHA2::Type::SHA2_256);
	sha.Init();
	sha.AddBuffer("nebula_", 7);
	sha.AddBuffer("crypto_test", 11);

	EXPECT_EQ(sha.Get(), crypto::Hash(crypto::HashType::SHA2_256, "nebula_crypto_test"));
}

namespace
{
	// RFC 2202 / RFC 4231 Test Case 2 는 key="Jefe", data="what do ya want for nothing?" 을 쓴다.
	ne::string_t HmacOf(const crypto::HashType _type)
	{
		auto key = crypto::HMACKey::Create(_type, "Jefe");
		EXPECT_TRUE(key.IsOk()) << key.Error().What();

		return key.Value().Generate("what do ya want for nothing?");
	}
}

// RFC 2202 / RFC 4231 test vectors
TEST(HashTest, HmacMd5) { EXPECT_EQ(HmacOf(crypto::HashType::MD5), "750c783e6ab0b503eaa86e310a5db738"); }

TEST(HashTest, HmacSha1) { EXPECT_EQ(HmacOf(crypto::HashType::SHA1), "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79"); }

TEST(HashTest, HmacSha256) { EXPECT_EQ(HmacOf(crypto::HashType::SHA2_256), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"); }

TEST(HashTest, HmacSha512)
{
	EXPECT_EQ(HmacOf(crypto::HashType::SHA2_512),
			  "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737");
}

// CRC32 는 체크섬이라 MAC 으로 쓸 수 없다 — API 가 거부해야 한다(조용히 약한 MAC 을 만들지 않음).
TEST(HashTest, HmacRejectsNonCryptographicHash)
{
	auto rejected = crypto::HMACKey::Create(crypto::HashType::CRC32, "Jefe");
	ASSERT_TRUE(rejected.IsError());
	EXPECT_EQ(rejected.Error().Kind(), crypto::CryptoErrorKind::INVALID_INPUT);

	EXPECT_TRUE(crypto::Hmac(crypto::HashType::CRC32, "Jefe", "msg").IsError());
	EXPECT_FALSE(crypto::HmacVerify(crypto::HashType::CRC32, "Jefe", "msg", "00000000"));
}

// HMAC Verify: 상수시간 비교 기반 검증
TEST(HashTest, HmacVerify)
{
	auto key = crypto::HMACKey::Create(crypto::HashType::MD5, "Jefe");
	ASSERT_TRUE(key.IsOk());

	// 올바른 MAC(알려진 RFC 2202 벡터) → true
	EXPECT_TRUE(key.Value().Verify("what do ya want for nothing?", "750c783e6ab0b503eaa86e310a5db738"));
	// 틀린 MAC → false
	EXPECT_FALSE(key.Value().Verify("what do ya want for nothing?", "00000000000000000000000000000000"));
	// 길이가 다른 MAC → false
	EXPECT_FALSE(key.Value().Verify("what do ya want for nothing?", "abcd"));
}

// 한 줄 진입점 — 키를 한 번만 쓰는 경우. 객체 경로와 결과가 같아야 한다.
TEST(HashTest, HmacOneLiner)
{
	auto md5 = crypto::Hmac(crypto::HashType::MD5, "Jefe", "what do ya want for nothing?");
	ASSERT_TRUE(md5.IsOk()) << md5.Error().What();
	EXPECT_EQ(md5.Value(), "750c783e6ab0b503eaa86e310a5db738");

	auto sha256 = crypto::Hmac(crypto::HashType::SHA2_256, "Jefe", "what do ya want for nothing?");
	ASSERT_TRUE(sha256.IsOk());
	EXPECT_EQ(sha256.Value(), HmacOf(crypto::HashType::SHA2_256));

	EXPECT_TRUE(crypto::HmacVerify(crypto::HashType::MD5, "Jefe", "what do ya want for nothing?", "750c783e6ab0b503eaa86e310a5db738"));
	EXPECT_FALSE(crypto::HmacVerify(crypto::HashType::MD5, "Jefe", "what do ya want for nothing?", "00000000000000000000000000000000"));
	EXPECT_FALSE(crypto::HmacVerify(crypto::HashType::MD5, "wrong-key", "what do ya want for nothing?", "750c783e6ab0b503eaa86e310a5db738"));
}

// ───────────────────────── HMAC 증분/파일 ─────────────────────────

// 조각째 넣은 결과가 한 번에 넣은 것과 같아야 한다(스트리밍의 유일한 정확성 기준).
TEST(HashTest, HmacStreamMatchesOneShot)
{
	auto key = crypto::HMACKey::Create(crypto::HashType::SHA2_256, "Jefe");
	ASSERT_TRUE(key.IsOk());

	auto stream = key.Value().BeginStream();
	ASSERT_TRUE(stream.IsOk()) << stream.Error().What();

	stream.Value().Update("what do ya ");
	stream.Value().Update("want for ");
	stream.Value().Update("nothing?");

	EXPECT_EQ(stream.Value().Final(), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
	// 반복 호출해도 같은 값(재계산하지 않음).
	EXPECT_EQ(stream.Value().Final(), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST(HashTest, HmacStreamEmptyAndReset)
{
	auto key = crypto::HMACKey::Create(crypto::HashType::SHA2_256, "k");
	ASSERT_TRUE(key.IsOk());

	auto stream = key.Value().BeginStream();
	ASSERT_TRUE(stream.IsOk());

	// 조각을 하나도 넣지 않으면 빈 메시지의 MAC.
	EXPECT_EQ(stream.Value().Final(), key.Value().Generate(""));

	// Reset 후 다시 계산할 수 있다.
	stream.Value().Reset();
	stream.Value().Update("payload");
	EXPECT_EQ(stream.Value().Final(), key.Value().Generate("payload"));

	// 확정 후의 Update 는 무시된다(값이 바뀌지 않음).
	const ne::string_t before = stream.Value().Final();
	stream.Value().Update("ignored");
	EXPECT_EQ(stream.Value().Final(), before);
}

TEST(HashTest, HmacStreamManySmallChunks)
{
	auto key = crypto::HMACKey::Create(crypto::HashType::SHA2_512, "chunky");
	ASSERT_TRUE(key.IsOk());

	const ne::string_t message(10000, 'x');

	auto stream = key.Value().BeginStream();
	ASSERT_TRUE(stream.IsOk());
	for (std::size_t offset = 0; offset < message.size(); offset += 7) stream.Value().Update(ne::string_view_t(message).substr(offset, 7));

	EXPECT_EQ(stream.Value().Final(), key.Value().Generate(message));
}

// 파일 MAC: 픽스처 내용을 직접 읽어 만든 MAC 과 일치해야 한다.
TEST(HashTest, HmacFileMatchesInMemory)
{
	std::FILE* file = nullptr;
#if defined(_WIN32)
	if (::fopen_s(&file, TestFilePath().c_str(), "rb") != 0) file = nullptr;
#else
	file = std::fopen(TestFilePath().c_str(), "rb");
#endif
	ASSERT_NE(file, nullptr);

	ne::string_t contents;
	char buffer[512];
	std::size_t length = 0;
	while ((length = std::fread(buffer, 1, sizeof(buffer), file)) > 0) contents.append(buffer, length);
	std::fclose(file);

	auto viaFile = crypto::HmacFile(crypto::HashType::SHA2_256, "file-key", TestFilePath());
	ASSERT_TRUE(viaFile.IsOk()) << viaFile.Error().What();

	auto key = crypto::HMACKey::Create(crypto::HashType::SHA2_256, "file-key");
	ASSERT_TRUE(key.IsOk());
	EXPECT_EQ(viaFile.Value(), key.Value().Generate(contents));
}

TEST(HashTest, HmacFileReportsMissingFileAndBadHash)
{
	auto missing = crypto::HmacFile(crypto::HashType::SHA2_256, "k", "no_such_file.bin");
	ASSERT_TRUE(missing.IsError());
	EXPECT_EQ(missing.Error().Kind(), crypto::CryptoErrorKind::INVALID_INPUT);

	EXPECT_TRUE(crypto::HmacFile(crypto::HashType::CRC32, "k", TestFilePath()).IsError());
}



// 상수시간 비교 유틸(내부 — HMAC Verify 가 의존)
TEST(HashTest, ConstantTimeEquals)
{
	EXPECT_TRUE(crypto::internal::ConstantTimeEquals("abc", "abc"));
	EXPECT_TRUE(crypto::internal::ConstantTimeEquals("", ""));
	EXPECT_FALSE(crypto::internal::ConstantTimeEquals("abc", "abd"));
	EXPECT_FALSE(crypto::internal::ConstantTimeEquals("abc", "abcd")); // 길이 상이
}
