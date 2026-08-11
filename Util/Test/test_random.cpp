//
// Created by hscloud on 26. 8. 3.
//
// SecureRandom(CSPRNG) 계약 검증 — Fill 성공/실패 보고, URBG 모델링(std 분포와의 조합).

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <random>
#include "Util/SecureRandom.h"



using ne::util::SecureRandom;

// Fill 은 요청한 길이를 전부 채우고 true 를 반환한다. (전부 0 인 64바이트가 나올 확률은 2^-512 — 실질 불가능)
TEST(SecureRandomTest, FillProducesEntropy)
{
	SecureRandom rng;

	std::array<ne::byte_t, 64> buffer{};
	ASSERT_TRUE(rng.Fill(buffer.data(), buffer.size()));
	EXPECT_TRUE(std::ranges::any_of(buffer, [](const ne::byte_t _b) { return _b != 0; }));
}

// 잘못된 인자 계약: nullptr 은 false, 길이 0 은 no-op 성공.
TEST(SecureRandomTest, FillArgumentContract)
{
	SecureRandom rng;

	EXPECT_FALSE(rng.Fill(nullptr, 16));

	ne::byte_t dummy = 0;
	EXPECT_TRUE(rng.Fill(&dummy, 0));
}

// Next()/operator() 는 호출마다 새 값을 뽑는다. (64비트 두 번이 연속으로 같을 확률 2^-64)
TEST(SecureRandomTest, NextVaries)
{
	SecureRandom rng;

	const auto first = rng.Next();
	const auto second = rng();
	EXPECT_NE(first, second);
}

// UniformRandomBitGenerator 모델링 — std::uniform_int_distribution 과 조합해 범위를 지킨다.
TEST(SecureRandomTest, ComposesWithUniformIntDistribution)
{
	SecureRandom rng;
	std::uniform_int_distribution<ne::uint_t> dist(1, 255);

	for (ne::int_t i = 0; i < 256; ++i)
	{
		const auto value = dist(rng);
		ASSERT_GE(value, 1u);
		ASSERT_LE(value, 255u);
	}
}
