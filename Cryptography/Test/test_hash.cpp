//
// Created by nebula on 24. 11. 10.
//

#include <gtest/gtest.h>
#include <cstring>
#include "Hash/Hash.h"
#include "Hash/Factory/HashFactory.h"
#include "HMAC/HMAC.h"



namespace crypto = ne::crypto;

TEST(HashTest, Crc32String)
{
	auto result = crypto::Hash(crypto::HashType::CRC32, "nebula_crypto_test");
	EXPECT_EQ(result, "C7094B48");

	ne::char_t buffer[64] = { 0, };
	crypto::HashFactory::Create(crypto::HashType::CRC32)->GetHashFromString(buffer, sizeof(buffer), "nebula_crypto_test");
	EXPECT_STREQ(buffer, "C7094B48");
}

TEST(HashTest, Crc32File)
{
	auto result = crypto::HashFile(crypto::HashType::CRC32, R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_EQ(result, "45632F7C");

	ne::char_t buffer[64] = { 0, };
	crypto::HashFactory::Create(crypto::HashType::CRC32)->GetHashFromFile(buffer, sizeof(buffer), R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_STREQ(buffer, "45632F7C");
}

TEST(HashTest, Md5String)
{
	auto result = crypto::Hash(crypto::HashType::MD5, "nebula_crypto_test");
	EXPECT_EQ(result, "0ef182ba075eff6de23c73d4e9a409a8");

	ne::char_t buffer[64] = { 0, };
	crypto::HashFactory::Create(crypto::HashType::MD5)->GetHashFromString(buffer, sizeof(buffer), "nebula_crypto_test");
	EXPECT_STREQ(buffer, "0ef182ba075eff6de23c73d4e9a409a8");
}

TEST(HashTest, Md5File)
{
	auto result = crypto::HashFile(crypto::HashType::MD5, R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_EQ(result, "0c844ec323b285191143c8481e2516cf");

	ne::char_t buffer[64] = { 0, };
	crypto::HashFactory::Create(crypto::HashType::MD5)->GetHashFromFile(buffer, sizeof(buffer), R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_STREQ(buffer, "0c844ec323b285191143c8481e2516cf");
}

TEST(HashTest, Sha1String)
{
	auto result = crypto::Hash(crypto::HashType::SHA1, "nebula_crypto_test");
	EXPECT_EQ(result, "534b46800f03c633f58552905c3429c0f25fbb4b");

	ne::char_t buffer[64] = { 0, };
	crypto::HashFactory::Create(crypto::HashType::SHA1)->GetHashFromString(buffer, sizeof(buffer), "nebula_crypto_test");
	EXPECT_STREQ(buffer, "534b46800f03c633f58552905c3429c0f25fbb4b");
}

TEST(HashTest, Sha1File)
{
	auto result = crypto::HashFile(crypto::HashType::SHA1, R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_EQ(result, "23189a05ce284ab5a01db409e82d29131bab266f");

	ne::char_t buffer[64] = { 0, };
	crypto::HashFactory::Create(crypto::HashType::SHA1)->GetHashFromFile(buffer, sizeof(buffer), R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_STREQ(buffer, "23189a05ce284ab5a01db409e82d29131bab266f");
}

TEST(HashTest, Sha2String)
{
	auto result = crypto::Hash(crypto::HashType::SHA2_224, "nebula_crypto_test");
	EXPECT_EQ(result, "42b23380642dca8c745a7fefee33401b9a5f88282e74abc7779315a2");

	result = crypto::Hash(crypto::HashType::SHA2_256, "nebula_crypto_test");
	EXPECT_EQ(result, "57e8c9b95dae156a76f6749d8ae381a7dedd00e36c4bab305d7fc96b08a1cafd");

	result = crypto::Hash(crypto::HashType::SHA2_384, "nebula_crypto_test");
	EXPECT_EQ(result, "f92d240bcfa2e062246160716ef3c44d4619e02a035108534ae404da4b3c89411e1122afbf3320236cd0d42bc351ba98");

	result = crypto::Hash(crypto::HashType::SHA2_512, "nebula_crypto_test");
	EXPECT_EQ(result, "458027ded8e8916b677e616f3565b2fbf5ce95b4c936af9ad11901af0393faec52278b42416b7a9e46a720c59acebcf7e7f7ed0ad382159a00c63864b920eb94");

	ne::char_t buffer[256] = { 0, };
	crypto::HashFactory::Create(crypto::HashType::SHA2_224)->GetHashFromString(buffer, sizeof(buffer), "nebula_crypto_test");
	EXPECT_STREQ(buffer, "42b23380642dca8c745a7fefee33401b9a5f88282e74abc7779315a2");

	memset(buffer, 0, sizeof(buffer));
	crypto::HashFactory::Create(crypto::HashType::SHA2_256)->GetHashFromString(buffer, sizeof(buffer), "nebula_crypto_test");
	EXPECT_STREQ(buffer, "57e8c9b95dae156a76f6749d8ae381a7dedd00e36c4bab305d7fc96b08a1cafd");

	memset(buffer, 0, sizeof(buffer));
	crypto::HashFactory::Create(crypto::HashType::SHA2_384)->GetHashFromString(buffer, sizeof(buffer), "nebula_crypto_test");
	EXPECT_STREQ(buffer, "f92d240bcfa2e062246160716ef3c44d4619e02a035108534ae404da4b3c89411e1122afbf3320236cd0d42bc351ba98");

	memset(buffer, 0, sizeof(buffer));
	crypto::HashFactory::Create(crypto::HashType::SHA2_512)->GetHashFromString(buffer, sizeof(buffer), "nebula_crypto_test");
	EXPECT_STREQ(buffer, "458027ded8e8916b677e616f3565b2fbf5ce95b4c936af9ad11901af0393faec52278b42416b7a9e46a720c59acebcf7e7f7ed0ad382159a00c63864b920eb94");
}

TEST(HashTest, Sha2File)
{
	auto result = crypto::HashFile(crypto::HashType::SHA2_224, R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_EQ(result, "cc3394f255e4800e1ab2766871390aa9007c6ba45a0ddbdf813b6c35");

	result = crypto::HashFile(crypto::HashType::SHA2_256, R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_EQ(result, "39f7a637bbed6db0c0db3914fd2d0521c9d3edde5b4f698ffb4872ce0c925377");

	result = crypto::HashFile(crypto::HashType::SHA2_384, R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_EQ(result, "e4f194aec15e2dee857fd847cd946336e623aef53bdcb6033aae7d2cdf29e1d8c4b928601ec57d45cea5666e68198cd1");

	result = crypto::HashFile(crypto::HashType::SHA2_512, R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_EQ(result, "6ffae33bf773f4ff595e676b40f5a9ca1aa88c31c7450fbcbfe8358fbcc7be6b2c3f6b7e9aa0ecc976d3ae114fb7424d52b6008aab4c29510385d8bc54f81abd");

	ne::char_t buffer[256] = { 0, };
	crypto::HashFactory::Create(crypto::HashType::SHA2_224)->GetHashFromFile(buffer, sizeof(buffer), R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_STREQ(buffer, "cc3394f255e4800e1ab2766871390aa9007c6ba45a0ddbdf813b6c35");

	memset(buffer, 0, sizeof(buffer));
	crypto::HashFactory::Create(crypto::HashType::SHA2_256)->GetHashFromFile(buffer, sizeof(buffer), R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_STREQ(buffer, "39f7a637bbed6db0c0db3914fd2d0521c9d3edde5b4f698ffb4872ce0c925377");

	memset(buffer, 0, sizeof(buffer));
	crypto::HashFactory::Create(crypto::HashType::SHA2_384)->GetHashFromFile(buffer, sizeof(buffer), R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_STREQ(buffer, "e4f194aec15e2dee857fd847cd946336e623aef53bdcb6033aae7d2cdf29e1d8c4b928601ec57d45cea5666e68198cd1");

	memset(buffer, 0, sizeof(buffer));
	crypto::HashFactory::Create(crypto::HashType::SHA2_512)->GetHashFromFile(buffer, sizeof(buffer), R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_STREQ(buffer, "6ffae33bf773f4ff595e676b40f5a9ca1aa88c31c7450fbcbfe8358fbcc7be6b2c3f6b7e9aa0ecc976d3ae114fb7424d52b6008aab4c29510385d8bc54f81abd");
}

TEST(HashTest, Sha3String)
{
	auto result = crypto::Hash(crypto::HashType::SHA3_224, "nebula_crypto_test");
	EXPECT_EQ(result, "d48305ca85f518bed9fe6dedb66c4f30cf1969fe9fba4d412d2e93c9");

	result = crypto::Hash(crypto::HashType::SHA3_256, "nebula_crypto_test");
	EXPECT_EQ(result, "d79a9c8f568cdd7fcf062793dd894f957d11009050606907ea008518199bca7f");

	result = crypto::Hash(crypto::HashType::SHA3_384, "nebula_crypto_test");
	EXPECT_EQ(result, "2d3fc82307d901de619b10957f0c906a05c60e3e822b97f9347e79ea6b9125d0c1fe22f6ea57a009f89b92b9a2d9c60e");

	result = crypto::Hash(crypto::HashType::SHA3_512, "nebula_crypto_test");
	EXPECT_EQ(result, "db037c67c891b44886beeba579da9cd8ecd49ce39d25540c4eebd200e2596c253ded3641711c2bc7c5af1cf999017064cef89c20abc6e7514041a2445f9203d1");

	ne::char_t buffer[256] = { 0, };
	crypto::HashFactory::Create(crypto::HashType::SHA3_224)->GetHashFromString(buffer, sizeof(buffer), "nebula_crypto_test");
	EXPECT_STREQ(buffer, "d48305ca85f518bed9fe6dedb66c4f30cf1969fe9fba4d412d2e93c9");

	memset(buffer, 0, sizeof(buffer));
	crypto::HashFactory::Create(crypto::HashType::SHA3_256)->GetHashFromString(buffer, sizeof(buffer), "nebula_crypto_test");
	EXPECT_STREQ(buffer, "d79a9c8f568cdd7fcf062793dd894f957d11009050606907ea008518199bca7f");

	memset(buffer, 0, sizeof(buffer));
	crypto::HashFactory::Create(crypto::HashType::SHA3_384)->GetHashFromString(buffer, sizeof(buffer), "nebula_crypto_test");
	EXPECT_STREQ(buffer, "2d3fc82307d901de619b10957f0c906a05c60e3e822b97f9347e79ea6b9125d0c1fe22f6ea57a009f89b92b9a2d9c60e");

	memset(buffer, 0, sizeof(buffer));
	crypto::HashFactory::Create(crypto::HashType::SHA3_512)->GetHashFromString(buffer, sizeof(buffer), "nebula_crypto_test");
	EXPECT_STREQ(buffer, "db037c67c891b44886beeba579da9cd8ecd49ce39d25540c4eebd200e2596c253ded3641711c2bc7c5af1cf999017064cef89c20abc6e7514041a2445f9203d1");
}

TEST(HashTest, Sha3File)
{
	auto result = crypto::HashFile(crypto::HashType::SHA3_224, R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_EQ(result, "6ef7cd5b40a10e4db064745cefea4e634f92d8703052a83fe57db2d1");

	result = crypto::HashFile(crypto::HashType::SHA3_256, R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_EQ(result, "9eb633197ac9ac9946421ef0c662c4f7e57967021aa2b57401ab0763745d5fe0");

	result = crypto::HashFile(crypto::HashType::SHA3_384, R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_EQ(result, "0bcca95fe73112184d56abce905b12d4dbcd9f6323eb060b9b695d1a143c4aa1e94aa8aae996ad2156b691a76b01740d");

	result = crypto::HashFile(crypto::HashType::SHA3_512, R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_EQ(result, "63646280705f8478ff89341a1d493c82efc58d9d977be40202b0e636670e1ccec4f9e7ca1bc8d3943dbf440a5ef07a79c34d68535a35a3a8262d8cf875843abc");

	ne::char_t buffer[256] = { 0, };
	crypto::HashFactory::Create(crypto::HashType::SHA3_224)->GetHashFromFile(buffer, sizeof(buffer), R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_STREQ(buffer, "6ef7cd5b40a10e4db064745cefea4e634f92d8703052a83fe57db2d1");

	memset(buffer, 0, sizeof(buffer));
	crypto::HashFactory::Create(crypto::HashType::SHA3_256)->GetHashFromFile(buffer, sizeof(buffer), R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_STREQ(buffer, "9eb633197ac9ac9946421ef0c662c4f7e57967021aa2b57401ab0763745d5fe0");

	memset(buffer, 0, sizeof(buffer));
	crypto::HashFactory::Create(crypto::HashType::SHA3_384)->GetHashFromFile(buffer, sizeof(buffer), R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_STREQ(buffer, "0bcca95fe73112184d56abce905b12d4dbcd9f6323eb060b9b695d1a143c4aa1e94aa8aae996ad2156b691a76b01740d");

	memset(buffer, 0, sizeof(buffer));
	crypto::HashFactory::Create(crypto::HashType::SHA3_512)->GetHashFromFile(buffer, sizeof(buffer), R"(E:\_PROJECT\_LIB\Nebula\_bin\test\Nebula hash test.txt)");
	EXPECT_STREQ(buffer, "63646280705f8478ff89341a1d493c82efc58d9d977be40202b0e636670e1ccec4f9e7ca1bc8d3943dbf440a5ef07a79c34d68535a35a3a8262d8cf875843abc");
}

// RFC 2202 / RFC 4231 test vectors
TEST(HashTest, HmacMd5)
{
	// RFC 2202 Test Case 2: key="Jefe", data="what do ya want for nothing?"
	auto result = crypto::HMACKey::Create(crypto::HashType::MD5, "Jefe").Generate("what do ya want for nothing?");
	EXPECT_EQ(result, "750c783e6ab0b503eaa86e310a5db738");
}

TEST(HashTest, HmacSha1)
{
	// RFC 2202 Test Case 2: key="Jefe", data="what do ya want for nothing?"
	auto result = crypto::HMACKey::Create(crypto::HashType::SHA1, "Jefe").Generate("what do ya want for nothing?");
	EXPECT_EQ(result, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");
}

TEST(HashTest, HmacSha256)
{
	// RFC 4231 Test Case 2: key="Jefe", data="what do ya want for nothing?"
	auto result = crypto::HMACKey::Create(crypto::HashType::SHA2_256, "Jefe").Generate("what do ya want for nothing?");
	EXPECT_EQ(result, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST(HashTest, HmacSha512)
{
	// RFC 4231 Test Case 2: key="Jefe", data="what do ya want for nothing?"
	auto result = crypto::HMACKey::Create(crypto::HashType::SHA2_512, "Jefe").Generate("what do ya want for nothing?");
	EXPECT_EQ(result, "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737");
}
