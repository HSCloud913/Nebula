//
// Created by nebula
//

#include <gtest/gtest.h>
#include "Util/Base64.h"

namespace util = ne::util;

TEST(Base64Test, EncodeStandard)
{
	EXPECT_EQ(util::Base64::Encode("123456789"), "MTIzNDU2Nzg5");
	EXPECT_EQ(util::Base64::Encode("Hello, World!"), "SGVsbG8sIFdvcmxkIQ==");
	EXPECT_EQ(util::Base64::Encode(""), "");
}

TEST(Base64Test, DecodeStandard)
{
	const auto digits = util::Base64::Decode("MTIzNDU2Nzg5");
	ASSERT_TRUE(digits.IsOk());
	EXPECT_EQ(digits.Value(), "123456789");

	const auto hello = util::Base64::Decode("SGVsbG8sIFdvcmxkIQ==");
	ASSERT_TRUE(hello.IsOk());
	EXPECT_EQ(hello.Value(), "Hello, World!");
}

// 안전 실패: 잘못된 입력은 오염 대신 Err
TEST(Base64Test, DecodeRejectsInvalid)
{
	EXPECT_TRUE(util::Base64::Decode("@@@@").IsError()); // 알파벳 밖 문자
	EXPECT_TRUE(util::Base64::Decode("AB=C").IsError()); // '=' 가 중간에
	EXPECT_TRUE(util::Base64::Decode("A").IsError());    // 잘린 입력(6비트 하나)
	EXPECT_TRUE(util::Base64::Decode("-_-_").IsError()); // URL 문자를 표준 디코더에
}

// 패딩 on/off 독립 지정
TEST(Base64Test, PaddingOption)
{
	EXPECT_EQ(util::Base64::Encode("Hi", true), "SGk=");
	EXPECT_EQ(util::Base64::Encode("Hi", false), "SGk");

	EXPECT_EQ(util::Base64::Decode("SGk=").Value(), "Hi"); // 패딩 있어도
	EXPECT_EQ(util::Base64::Decode("SGk").Value(), "Hi");  // 없어도
}

TEST(Base64Test, UrlSafe)
{
	EXPECT_EQ(util::Base64::EncodeURL(ne::string_t("\xfb\xff\xfe", 3)), "-__-"); // 기본 패딩 없음
	EXPECT_EQ(util::Base64::EncodeURL("Hello, World!"), "SGVsbG8sIFdvcmxkIQ");

	const auto binary = util::Base64::DecodeURL("-__-");
	ASSERT_TRUE(binary.IsOk());
	EXPECT_EQ(binary.Value(), ne::string_t("\xfb\xff\xfe", 3));
	EXPECT_EQ(util::Base64::DecodeURL("SGVsbG8sIFdvcmxkIQ").Value(), "Hello, World!");
}

// 모든 바이트값 왕복
TEST(Base64Test, RoundTripAllBytes)
{
	ne::string_t data;
	for (int i = 0; i < 256; ++i) data += static_cast<char>(i);

	const auto decoded = util::Base64::Decode(util::Base64::Encode(data));
	ASSERT_TRUE(decoded.IsOk());
	EXPECT_EQ(decoded.Value(), data);
}
