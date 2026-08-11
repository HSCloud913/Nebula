//
// Created by hscloud on 24. 9. 8.
//

#include <gtest/gtest.h>
#include "Util/Ascii.h"



TEST(AsciiTest, IsAscii)
{
	EXPECT_TRUE(ne::util::Ascii::IsAscii(65));   // 'A'
	EXPECT_TRUE(ne::util::Ascii::IsAscii(97));   // 'a'
	EXPECT_FALSE(ne::util::Ascii::IsAscii(128)); // ASCII 범위 밖 문자
}

TEST(AsciiTest, IsSpace)
{
	EXPECT_TRUE(ne::util::Ascii::IsSpace(32));  // ' '
	EXPECT_FALSE(ne::util::Ascii::IsSpace(65)); // 'A'
}

TEST(AsciiTest, IsPunct)
{
	EXPECT_TRUE(ne::util::Ascii::IsPunct(33));  // '!' (구두점 문자)
	EXPECT_FALSE(ne::util::Ascii::IsPunct(65)); // 'A'
}

TEST(AsciiTest, IsDigit)
{
	EXPECT_TRUE(ne::util::Ascii::IsDigit(48));  // '0'
	EXPECT_TRUE(ne::util::Ascii::IsDigit(57));  // '9'
	EXPECT_FALSE(ne::util::Ascii::IsDigit(65)); // 'A'
}

TEST(AsciiTest, IsHexDigit)
{
	EXPECT_TRUE(ne::util::Ascii::IsHexDigit(48));  // '0'
	EXPECT_TRUE(ne::util::Ascii::IsHexDigit(57));  // '9'
	EXPECT_TRUE(ne::util::Ascii::IsHexDigit(65));  // 'A'
	EXPECT_TRUE(ne::util::Ascii::IsHexDigit(102)); // 'f'
	EXPECT_FALSE(ne::util::Ascii::IsHexDigit(71)); // 'G'
}

TEST(AsciiTest, IsAlpha)
{
	EXPECT_TRUE(ne::util::Ascii::IsAlpha(65));  // 'A'
	EXPECT_TRUE(ne::util::Ascii::IsAlpha(97));  // 'a'
	EXPECT_FALSE(ne::util::Ascii::IsAlpha(48)); // '0'
}

TEST(AsciiTest, IsLower)
{
	EXPECT_TRUE(ne::util::Ascii::IsLower(97));  // 'a'
	EXPECT_FALSE(ne::util::Ascii::IsLower(65)); // 'A'
}

TEST(AsciiTest, IsUpper)
{
	EXPECT_TRUE(ne::util::Ascii::IsUpper(65));  // 'A'
	EXPECT_FALSE(ne::util::Ascii::IsUpper(97)); // 'a'
}

TEST(AsciiTest, Lower)
{
	EXPECT_EQ(ne::util::Ascii::Lower(65), 97); // 'A'
	EXPECT_EQ(ne::util::Ascii::Lower(97), 97); // 'a'
}

TEST(AsciiTest, Upper)
{
	EXPECT_EQ(ne::util::Ascii::Upper(97), 65); // 'a'
	EXPECT_EQ(ne::util::Ascii::Upper(65), 65); // 'A'
}

TEST(AsciiTest, IsAlphaNumeric)
{
	EXPECT_TRUE(ne::util::Ascii::IsAlphaNumeric(65));  // 'A'
	EXPECT_TRUE(ne::util::Ascii::IsAlphaNumeric(48));  // '0'
	EXPECT_FALSE(ne::util::Ascii::IsAlphaNumeric(33)); // '!'
}

TEST(AsciiTest, IsControl)
{
	EXPECT_TRUE(ne::util::Ascii::IsControl(0));   // NUL
	EXPECT_TRUE(ne::util::Ascii::IsControl(27));  // ESC
	EXPECT_TRUE(ne::util::Ascii::IsControl(127)); // DEL
	EXPECT_FALSE(ne::util::Ascii::IsControl(32)); // ' '
	EXPECT_FALSE(ne::util::Ascii::IsControl(65)); // 'A'
}

TEST(AsciiTest, IsGraph)
{
	EXPECT_TRUE(ne::util::Ascii::IsGraph(33));  // '!'
	EXPECT_TRUE(ne::util::Ascii::IsGraph(65));  // 'A'
	EXPECT_TRUE(ne::util::Ascii::IsGraph(126)); // '~'
	EXPECT_FALSE(ne::util::Ascii::IsGraph(32)); // ' ' (space는 graph 아님)
	EXPECT_FALSE(ne::util::Ascii::IsGraph(0));  // NUL
}

TEST(AsciiTest, IsPrint)
{
	EXPECT_TRUE(ne::util::Ascii::IsPrint(32));   // ' '
	EXPECT_TRUE(ne::util::Ascii::IsPrint(65));   // 'A'
	EXPECT_TRUE(ne::util::Ascii::IsPrint(126));  // '~'
	EXPECT_FALSE(ne::util::Ascii::IsPrint(0));   // NUL
	EXPECT_FALSE(ne::util::Ascii::IsPrint(127)); // DEL
}
