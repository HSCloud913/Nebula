//
// Created by hscloud on 24. 9. 8.
//

#include <gtest/gtest.h>
#include "Ascii.h"



TEST(AsciiTest, IsAscii)
{
    EXPECT_TRUE(ne::Ascii::IsAscii(65));  // 'A'
    EXPECT_TRUE(ne::Ascii::IsAscii(97));  // 'a'
    EXPECT_FALSE(ne::Ascii::IsAscii(128)); // ASCII 범위 밖 문자
}

TEST(AsciiTest, IsSpace)
{
    EXPECT_TRUE(ne::Ascii::IsSpace(32));  // ' '
    EXPECT_FALSE(ne::Ascii::IsSpace(65)); // 'A'
}

TEST(AsciiTest, IsPunct)
{
    EXPECT_TRUE(ne::Ascii::IsPunct(33));  // '!' (구두점 문자)
    EXPECT_FALSE(ne::Ascii::IsPunct(65)); // 'A'
}

TEST(AsciiTest, IsDigit)
{
    EXPECT_TRUE(ne::Ascii::IsDigit(48));  // '0'
    EXPECT_TRUE(ne::Ascii::IsDigit(57));  // '9'
    EXPECT_FALSE(ne::Ascii::IsDigit(65)); // 'A'
}

TEST(AsciiTest, IsHexDigit)
{
    EXPECT_TRUE(ne::Ascii::IsHexDigit(48));  // '0'
    EXPECT_TRUE(ne::Ascii::IsHexDigit(57));  // '9'
    EXPECT_TRUE(ne::Ascii::IsHexDigit(65));  // 'A'
    EXPECT_TRUE(ne::Ascii::IsHexDigit(102)); // 'f'
    EXPECT_FALSE(ne::Ascii::IsHexDigit(71)); // 'G'
}

TEST(AsciiTest, IsAlpha)
{
    EXPECT_TRUE(ne::Ascii::IsAlpha(65));  // 'A'
    EXPECT_TRUE(ne::Ascii::IsAlpha(97));  // 'a'
    EXPECT_FALSE(ne::Ascii::IsAlpha(48)); // '0'
}

TEST(AsciiTest, IsLower)
{
    EXPECT_TRUE(ne::Ascii::IsLower(97));  // 'a'
    EXPECT_FALSE(ne::Ascii::IsLower(65)); // 'A'
}

TEST(AsciiTest, IsUpper)
{
    EXPECT_TRUE(ne::Ascii::IsUpper(65));  // 'A'
    EXPECT_FALSE(ne::Ascii::IsUpper(97)); // 'a'
}

TEST(AsciiTest, Lower)
{
    EXPECT_EQ(ne::Ascii::Lower(65), 97);  // 'A'
    EXPECT_EQ(ne::Ascii::Lower(97), 97);  // 'a'
}

TEST(AsciiTest, Upper)
{
    EXPECT_EQ(ne::Ascii::Upper(97), 65);  // 'a'
    EXPECT_EQ(ne::Ascii::Upper(65), 65);  // 'A'
}

TEST(AsciiTest, IsAlphaNumeric)
{
    EXPECT_TRUE(ne::Ascii::IsAlphaNumeric(65));  // 'A'
    EXPECT_TRUE(ne::Ascii::IsAlphaNumeric(48));  // '0'
    EXPECT_FALSE(ne::Ascii::IsAlphaNumeric(33)); // '!'
}

TEST(AsciiTest, IsControl)
{
    EXPECT_TRUE(ne::Ascii::IsControl(0));    // NUL
    EXPECT_TRUE(ne::Ascii::IsControl(27));   // ESC
    EXPECT_TRUE(ne::Ascii::IsControl(127));  // DEL
    EXPECT_FALSE(ne::Ascii::IsControl(32));  // ' '
    EXPECT_FALSE(ne::Ascii::IsControl(65));  // 'A'
}

TEST(AsciiTest, IsGraph)
{
    EXPECT_TRUE(ne::Ascii::IsGraph(33));   // '!'
    EXPECT_TRUE(ne::Ascii::IsGraph(65));   // 'A'
    EXPECT_TRUE(ne::Ascii::IsGraph(126));  // '~'
    EXPECT_FALSE(ne::Ascii::IsGraph(32));  // ' ' (space는 graph 아님)
    EXPECT_FALSE(ne::Ascii::IsGraph(0));   // NUL
}

TEST(AsciiTest, IsPrint)
{
    EXPECT_TRUE(ne::Ascii::IsPrint(32));   // ' '
    EXPECT_TRUE(ne::Ascii::IsPrint(65));   // 'A'
    EXPECT_TRUE(ne::Ascii::IsPrint(126));  // '~'
    EXPECT_FALSE(ne::Ascii::IsPrint(0));   // NUL
    EXPECT_FALSE(ne::Ascii::IsPrint(127)); // DEL
}