//
// Created by hscloud on 24. 9. 8.
//

#include <gtest/gtest.h>
#include "Ascii.h"



TEST(AsciiTest, IsAscii)
{
    EXPECT_TRUE(NebulaAscii::IsAscii(65));  // 'A'
    EXPECT_TRUE(NebulaAscii::IsAscii(97));  // 'a'
    EXPECT_FALSE(NebulaAscii::IsAscii(128)); // ASCII 범위 밖 문자
}

TEST(AsciiTest, IsSpace)
{
    EXPECT_TRUE(NebulaAscii::IsSpace(32));  // ' '
    EXPECT_FALSE(NebulaAscii::IsSpace(65)); // 'A'
}

TEST(AsciiTest, IsPunct)
{
    EXPECT_TRUE(NebulaAscii::IsPunct(33));  // '!' (구두점 문자)
    EXPECT_FALSE(NebulaAscii::IsPunct(65)); // 'A'
}

TEST(AsciiTest, IsDigit)
{
    EXPECT_TRUE(NebulaAscii::IsDigit(48));  // '0'
    EXPECT_TRUE(NebulaAscii::IsDigit(57));  // '9'
    EXPECT_FALSE(NebulaAscii::IsDigit(65)); // 'A'
}

TEST(AsciiTest, IsHexDigit)
{
    EXPECT_TRUE(NebulaAscii::IsHexDigit(48));  // '0'
    EXPECT_TRUE(NebulaAscii::IsHexDigit(57));  // '9'
    EXPECT_TRUE(NebulaAscii::IsHexDigit(65));  // 'A'
    EXPECT_TRUE(NebulaAscii::IsHexDigit(102)); // 'f'
    EXPECT_FALSE(NebulaAscii::IsHexDigit(71)); // 'G'
}

TEST(AsciiTest, IsAlpha)
{
    EXPECT_TRUE(NebulaAscii::IsAlpha(65));  // 'A'
    EXPECT_TRUE(NebulaAscii::IsAlpha(97));  // 'a'
    EXPECT_FALSE(NebulaAscii::IsAlpha(48)); // '0'
}

TEST(AsciiTest, IsLower)
{
    EXPECT_TRUE(NebulaAscii::IsLower(97));  // 'a'
    EXPECT_FALSE(NebulaAscii::IsLower(65)); // 'A'
}

TEST(AsciiTest, IsUpper)
{
    EXPECT_TRUE(NebulaAscii::IsUpper(65));  // 'A'
    EXPECT_FALSE(NebulaAscii::IsUpper(97)); // 'a'
}

TEST(AsciiTest, Lower)
{
    EXPECT_EQ(NebulaAscii::Lower(65), 97);  // 'A'
    EXPECT_EQ(NebulaAscii::Lower(97), 97);  // 'a'
}

TEST(AsciiTest, Upper)
{
    EXPECT_EQ(NebulaAscii::Upper(97), 65);  // 'a'
    EXPECT_EQ(NebulaAscii::Upper(65), 65);  // 'A'
}

TEST(AsciiTest, IsAlphaNumeric)
{
    EXPECT_TRUE(NebulaAscii::IsAlphaNumeric(65));  // 'A'
    EXPECT_TRUE(NebulaAscii::IsAlphaNumeric(48));  // '0'
    EXPECT_FALSE(NebulaAscii::IsAlphaNumeric(33)); // '!'
}