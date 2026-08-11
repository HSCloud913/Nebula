//
// Created by hscloud on 26. 8. 6.
//

#include <gtest/gtest.h>
#include "Util/Hex.h"



using ne::util::Hex;

TEST(HexTest, EncodeProducesLowercase)
{
	EXPECT_EQ(Hex::Encode(""), "");
	EXPECT_EQ(Hex::Encode("A"), "41");
	EXPECT_EQ(Hex::Encode(ne::string_t("\x00\x0f\xff", 3)), "000fff");
}

TEST(HexTest, DecodeAcceptsBothCases)
{
	EXPECT_EQ(Hex::Decode("41").value(), "A");
	EXPECT_EQ(Hex::Decode("deadBEEF").value(), Hex::Decode("DEADbeef").value());
	EXPECT_EQ(Hex::Decode("").value(), "");
}

TEST(HexTest, DecodeRejectsMalformedInput)
{
	EXPECT_FALSE(Hex::Decode("4").has_value());   // 홀수 길이
	EXPECT_FALSE(Hex::Decode("4g").has_value());  // 알파벳 밖 문자
	EXPECT_FALSE(Hex::Decode("zz").has_value());
}

TEST(HexTest, RoundTripPreservesAllByteValues)
{
	ne::string_t all;
	for (ne::int_t i = 0; i < 256; ++i) all += static_cast<ne::char_t>(i);

	EXPECT_EQ(Hex::Decode(Hex::Encode(all)).value(), all);
}
