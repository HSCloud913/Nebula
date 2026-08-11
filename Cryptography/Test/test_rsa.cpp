//
// Created by nebula on 24. 11. 10.
//
// RSA + BigInt 검증. 키 생성이 매우 느리므로(1024비트 수십 초) 기능 테스트는 **고정 테스트 키페어**를
// 쓰고, 실제 Generate() 검증은 DISABLED 테스트로 분리했다(필요 시 --gtest_also_run_disabled_tests).

#include <gtest/gtest.h>
#include "Cryptography/RSA.h"
#include "../Internal/Math/BigInt.h"



namespace crypto = ne::crypto;

namespace
{
	// 이 저장소의 RSAKeyPair::Generate(RSA_1024) 로 뽑은 고정 키페어(테스트 전용 — 어디에도 쓰지 말 것).
	constexpr ne::lpcstr_t TestN = "90d841947684569d703daea2a38e1b118c376944b65a1e4cbb6a02c371858ee2ac77ac28e59352929b70fdb7d13156546d509e08db14b81c8b4db452f2f6d622"
								   "c62cbb86e64cd55f7041de03e15c46cfcfd1840b021385d4b6d06221b24724f727dd5aa4a4d8afd8a64c8b771a36321b5002b74a297bb0097b364479c60dc025";
	constexpr ne::lpcstr_t TestE = "10001";
	constexpr ne::lpcstr_t TestD = "4714f9ca04079a02156d0fe0dce7063dac541d230d6258704ba110ce1d4deffb29ac691e80dbb5b020fc6866e710914f497e40b013e3ad1ec4f6534249ddfce3"
								   "c2d2a116593f2af4d8ed33a0eb67c3930483c4675870900611bce6dc91d4fce535202ef5ed2da79d247bdbf5d7c226be577eb545ed48dc798039556f2b251599";

	crypto::RSAPublicKey TestPublicKey() { return crypto::RSAPublicKey{ TestN, TestE }; }
	crypto::RSAPrivateKey TestPrivateKey() { return crypto::RSAPrivateKey{ TestN, TestD }; }
}



// ───────────────────────── BigInt ─────────────────────────

TEST(RSATest, BigIntBasicArithmetic)
{
	using BI = crypto::internal::BigInt;
	EXPECT_EQ((BI(12u) + BI(34u)).ToHex(), "2e");
	EXPECT_EQ((BI(100u) - BI(37u)).ToHex(), "3f");
	EXPECT_EQ((BI(12u) * BI(34u)).ToHex(), "198");
	EXPECT_EQ((BI(100u) / BI(7u)).ToHex(), "e");
	EXPECT_EQ((BI(100u) % BI(7u)).ToHex(), "2");
}

TEST(RSATest, BigIntModPow)
{
	using BI = crypto::internal::BigInt;
	EXPECT_EQ(BI::ModPow(BI(2u), BI(10u), BI(1000u)).ToHex(), "18");
	EXPECT_EQ(BI::ModPow(BI(5u), BI(6u), BI(7u)).ToHex(), "1");
}

TEST(RSATest, BigIntModInverse)
{
	using BI = crypto::internal::BigInt;
	EXPECT_EQ(BI::ModInverse(BI(3u), BI(7u)).ToHex(), "5");
	EXPECT_EQ(BI::ModInverse(BI(2u), BI(5u)).ToHex(), "3");
}

TEST(RSATest, BigIntPrimality)
{
	using BI = crypto::internal::BigInt;
	EXPECT_TRUE(BI(2u).IsProbablyPrime());
	EXPECT_TRUE(BI(3u).IsProbablyPrime());
	EXPECT_TRUE(BI(17u).IsProbablyPrime());
	EXPECT_TRUE(BI(65537u).IsProbablyPrime());
	EXPECT_FALSE(BI(4u).IsProbablyPrime());
	EXPECT_FALSE(BI(100u).IsProbablyPrime());
}



// ───────────────────────── 암·복호 (고정 키페어) ─────────────────────────

TEST(RSATest, RoundTrip)
{
	const ne::string_t msg = "Hello RSA";

	auto ct = TestPublicKey().Encrypt(msg);
	ASSERT_TRUE(ct.IsOk()) << ct.Error().What();

	auto pt = TestPrivateKey().Decrypt(ct.Value());
	ASSERT_TRUE(pt.IsOk()) << pt.Error().What();
	EXPECT_EQ(pt.Value(), msg);
}

TEST(RSATest, RoundTripEmptyAndMaxLength)
{
	// 1024비트 = 128바이트, PKCS#1 v1.5 상한은 128-11 = 117바이트.
	for (const ne::string_t msg : { ne::string_t{}, ne::string_t(117, 'x') })
	{
		auto ct = TestPublicKey().Encrypt(msg);
		ASSERT_TRUE(ct.IsOk()) << ct.Error().What();

		auto pt = TestPrivateKey().Decrypt(ct.Value());
		ASSERT_TRUE(pt.IsOk()) << pt.Error().What();
		EXPECT_EQ(pt.Value(), msg);
	}
}

// PKCS#1 v1.5 패딩의 무작위 PS 때문에 같은 평문도 매번 다른 암호문이 나온다.
TEST(RSATest, EncryptDifferentEachTime)
{
	auto first = TestPublicKey().Encrypt("test");
	auto second = TestPublicKey().Encrypt("test");
	ASSERT_TRUE(first.IsOk());
	ASSERT_TRUE(second.IsOk());

	EXPECT_NE(first.Value(), second.Value());
}

// 키 크기 대비 너무 큰 평문은 예외가 아니라 MESSAGE_TOO_LARGE 로 보고한다.
TEST(RSATest, OversizedPlaintextIsReported)
{
	auto tooBig = TestPublicKey().Encrypt(ne::string_t(118, 'x')); // 상한 117 초과
	ASSERT_TRUE(tooBig.IsError());
	EXPECT_EQ(tooBig.Error().Kind(), crypto::CryptoErrorKind::MESSAGE_TOO_LARGE);
}

// 패딩이 깨진 암호문 복호도 예외가 아니라 MALFORMED_PADDING 으로 보고한다.
TEST(RSATest, MalformedCiphertextIsReported)
{
	auto broken = TestPrivateKey().Decrypt(ne::string_t(128, '\x01'));
	ASSERT_TRUE(broken.IsError());
	EXPECT_EQ(broken.Error().Kind(), crypto::CryptoErrorKind::MALFORMED_PADDING);
}



// ───────────────────────── 서명 / 검증 (RSASSA-PKCS1-v1_5) ─────────────────────────

TEST(RSATest, SignVerifyRoundTrip)
{
	const ne::string_t msg = "message to be signed";

	auto sig = TestPrivateKey().Sign(msg);
	ASSERT_TRUE(sig.IsOk()) << sig.Error().What();
	EXPECT_EQ(sig.Value().size(), 128u); // 1024비트 키 = 128바이트 서명

	EXPECT_TRUE(TestPublicKey().Verify(msg, sig.Value()));
}

// PKCS#1 v1.5 는 결정적 — 같은 키·메시지면 항상 같은 서명이 나온다.
TEST(RSATest, SignIsDeterministic)
{
	auto first = TestPrivateKey().Sign("same message");
	auto second = TestPrivateKey().Sign("same message");
	ASSERT_TRUE(first.IsOk());
	ASSERT_TRUE(second.IsOk());

	EXPECT_EQ(first.Value(), second.Value());
}

TEST(RSATest, VerifyRejectsTamperedInput)
{
	const ne::string_t msg = "authentic message";
	auto sig = TestPrivateKey().Sign(msg);
	ASSERT_TRUE(sig.IsOk());

	// 메시지가 바뀌면 실패
	EXPECT_FALSE(TestPublicKey().Verify("authentic messagf", sig.Value()));
	// 서명 1비트가 바뀌면 실패
	ne::string_t tampered = sig.Value();
	tampered[64] = static_cast<ne::char_t>(tampered[64] ^ 0x01);
	EXPECT_FALSE(TestPublicKey().Verify(msg, tampered));
	// 서명 길이가 다르면 실패
	EXPECT_FALSE(TestPublicKey().Verify(msg, sig.Value().substr(0, 127)));
	EXPECT_FALSE(TestPublicKey().Verify(msg, ""));
	// 다른 해시로 검증하면 실패
	EXPECT_FALSE(TestPublicKey().Verify(msg, sig.Value(), crypto::HashType::SHA2_512));
}

TEST(RSATest, SignSupportsMultipleHashesAndRejectsUnusable)
{
	for (const auto hashType : { crypto::HashType::SHA1, crypto::HashType::SHA2_256, crypto::HashType::SHA2_384, crypto::HashType::SHA2_512 })
	{
		auto sig = TestPrivateKey().Sign("multi-hash", hashType);
		ASSERT_TRUE(sig.IsOk()) << sig.Error().What();
		EXPECT_TRUE(TestPublicKey().Verify("multi-hash", sig.Value(), hashType));
	}

	// DER OID 가 없는 해시(CRC32/MD5/SHA3)는 서명에 쓸 수 없다.
	auto rejected = TestPrivateKey().Sign("nope", crypto::HashType::CRC32);
	ASSERT_TRUE(rejected.IsError());
	EXPECT_EQ(rejected.Error().Kind(), crypto::CryptoErrorKind::INVALID_INPUT);
	EXPECT_TRUE(TestPrivateKey().Sign("nope", crypto::HashType::SHA3_256).IsError());
}



// ───────────────────────── 직렬화 ─────────────────────────

TEST(RSATest, SerializeRoundTrip)
{
	const ne::string_t publicText = TestPublicKey().Serialize();
	EXPECT_TRUE(publicText.starts_with("NRSA1-PUB:"));

	auto publicKey = crypto::RSAPublicKey::Deserialize(publicText);
	ASSERT_TRUE(publicKey.IsOk()) << publicKey.Error().What();
	EXPECT_EQ(publicKey.Value().n, TestN);
	EXPECT_EQ(publicKey.Value().e, TestE);

	const ne::string_t privateText = TestPrivateKey().Serialize();
	EXPECT_TRUE(privateText.starts_with("NRSA1-PRV:"));

	auto privateKey = crypto::RSAPrivateKey::Deserialize(privateText);
	ASSERT_TRUE(privateKey.IsOk()) << privateKey.Error().What();
	EXPECT_EQ(privateKey.Value().n, TestN);
	EXPECT_EQ(privateKey.Value().d, TestD);

	// 되읽은 키로 실제 왕복이 되어야 한다.
	auto ct = publicKey.Value().Encrypt("via deserialized key");
	ASSERT_TRUE(ct.IsOk());
	EXPECT_EQ(privateKey.Value().Decrypt(ct.Value()).Value(), "via deserialized key");
}

TEST(RSATest, DeserializeRejectsMalformed)
{
	using PubKey = crypto::RSAPublicKey;

	EXPECT_TRUE(PubKey::Deserialize("").IsError());
	EXPECT_TRUE(PubKey::Deserialize("NRSA1-PRV:aa:bb").IsError());      // 태그 불일치(개인키)
	EXPECT_TRUE(PubKey::Deserialize("NRSA1-PUB:aabb").IsError());       // 구분자 없음
	EXPECT_TRUE(PubKey::Deserialize("NRSA1-PUB::bb").IsError());        // 빈 필드
	EXPECT_TRUE(PubKey::Deserialize("NRSA1-PUB:zz:bb").IsError());      // 비16진
	EXPECT_TRUE(crypto::RSAPrivateKey::Deserialize("NRSA1-PUB:aa:bb").IsError()); // 태그 교차
}



// ───────────────────────── 하이브리드 봉인 ─────────────────────────

TEST(RSATest, HybridRoundTrip)
{
	// RSA 직접 암호화 상한(117바이트)을 훨씬 넘는 데이터도 처리된다.
	const ne::string_t large(100 * 1024, 'x');

	auto sealed = crypto::RsaSeal(TestPublicKey(), large);
	ASSERT_TRUE(sealed.IsOk()) << sealed.Error().What();

	auto opened = crypto::RsaOpen(TestPrivateKey(), sealed.Value());
	ASSERT_TRUE(opened.IsOk()) << opened.Error().What();
	EXPECT_EQ(opened.Value(), large);
}

TEST(RSATest, HybridRoundTripEmptyAndBinary)
{
	ne::string_t binary;
	for (ne::int_t i = 0; i < 256; ++i) binary += static_cast<ne::char_t>(i);

	for (const ne::string_t& payload : { ne::string_t{}, binary })
	{
		auto sealed = crypto::RsaSeal(TestPublicKey(), payload);
		ASSERT_TRUE(sealed.IsOk());
		EXPECT_EQ(crypto::RsaOpen(TestPrivateKey(), sealed.Value()).Value(), payload);
	}
}

// 세션 키가 매번 새로 뽑히므로 같은 평문도 다른 봉투가 된다.
TEST(RSATest, HybridProducesDifferentEnvelopes)
{
	auto first = crypto::RsaSeal(TestPublicKey(), "same input");
	auto second = crypto::RsaSeal(TestPublicKey(), "same input");
	ASSERT_TRUE(first.IsOk());
	ASSERT_TRUE(second.IsOk());

	EXPECT_NE(first.Value(), second.Value());
	EXPECT_EQ(crypto::RsaOpen(TestPrivateKey(), first.Value()).Value(), "same input");
}

TEST(RSATest, HybridDetectsTampering)
{
	auto sealed = crypto::RsaSeal(TestPublicKey(), "authenticated payload");
	ASSERT_TRUE(sealed.IsOk());
	const ne::string_t original = sealed.Value();

	// 감싼 키 / IV / 암호문 / MAC 구간을 각각 1비트 뒤집는다.
	const std::size_t wrappedKeyOffset = 3;
	const std::size_t ivOffset = 3 + 128;
	const std::size_t ciphertextOffset = ivOffset + 16;
	const std::size_t macOffset = original.size() - 32;

	for (const std::size_t offset : { wrappedKeyOffset, ivOffset, ciphertextOffset, macOffset })
	{
		ne::string_t tampered = original;
		tampered[offset] = static_cast<ne::char_t>(tampered[offset] ^ 0x01);

		auto opened = crypto::RsaOpen(TestPrivateKey(), tampered);
		ASSERT_TRUE(opened.IsError()) << "offset " << offset << " 변조가 탐지되지 않았다";
		EXPECT_EQ(opened.Error().Kind(), crypto::CryptoErrorKind::AUTHENTICATION_FAILED);
	}
}

TEST(RSATest, HybridAssociatedDataMustMatch)
{
	auto sealed = crypto::RsaSeal(TestPublicKey(), "row payload", "record-42");
	ASSERT_TRUE(sealed.IsOk());

	EXPECT_EQ(crypto::RsaOpen(TestPrivateKey(), sealed.Value(), "record-42").Value(), "row payload");

	auto wrongContext = crypto::RsaOpen(TestPrivateKey(), sealed.Value(), "record-43");
	ASSERT_TRUE(wrongContext.IsError());
	EXPECT_EQ(wrongContext.Error().Kind(), crypto::CryptoErrorKind::AUTHENTICATION_FAILED);

	EXPECT_TRUE(crypto::RsaOpen(TestPrivateKey(), sealed.Value()).IsError()); // 문맥 누락
}

TEST(RSATest, HybridRejectsMalformedEnvelope)
{
	auto sealed = crypto::RsaSeal(TestPublicKey(), "payload");
	ASSERT_TRUE(sealed.IsOk());

	// 잘린 봉투
	EXPECT_TRUE(crypto::RsaOpen(TestPrivateKey(), "").IsError());
	EXPECT_TRUE(crypto::RsaOpen(TestPrivateKey(), ne::string_view_t(sealed.Value()).substr(0, 40)).IsError());

	// 모르는 버전
	ne::string_t future = sealed.Value();
	future[0] = static_cast<ne::char_t>(0x02);
	auto unsupported = crypto::RsaOpen(TestPrivateKey(), future);
	ASSERT_TRUE(unsupported.IsError());
	EXPECT_EQ(unsupported.Error().Kind(), crypto::CryptoErrorKind::UNSUPPORTED_VERSION);

	// 감싼 키 길이 필드가 봉투 크기와 안 맞음
	ne::string_t badLength = sealed.Value();
	badLength[1] = static_cast<ne::char_t>(0xFF);
	auto mismatched = crypto::RsaOpen(TestPrivateKey(), badLength);
	ASSERT_TRUE(mismatched.IsError());
	EXPECT_EQ(mismatched.Error().Kind(), crypto::CryptoErrorKind::INVALID_INPUT);
}



// ───────────────────────── 키 생성 (느림 — 필요 시에만) ─────────────────────────

// 소수 탐색을 반복해 수십 초가 걸리므로 기본 실행에서 제외한다.
// 확인이 필요하면: NebulaCryptographyTest --gtest_filter=RSATest.DISABLED_* --gtest_also_run_disabled_tests
TEST(RSATest, DISABLED_GenerateProducesWorkingKeyPair)
{
	auto kp = crypto::RSAKeyPair::Generate(crypto::RSAKeyPair::KeySize::RSA_1024);

	EXPECT_EQ(kp.publicKey.e, "10001");
	EXPECT_EQ(kp.publicKey.n, kp.privateKey.n);

	const ne::string_t msg = "generated key round trip";
	auto ct = kp.publicKey.Encrypt(msg);
	ASSERT_TRUE(ct.IsOk());

	auto pt = kp.privateKey.Decrypt(ct.Value());
	ASSERT_TRUE(pt.IsOk());
	EXPECT_EQ(pt.Value(), msg);
}
