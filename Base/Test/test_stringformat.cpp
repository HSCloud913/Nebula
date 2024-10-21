//
// Created by hsclo on 24. 10. 21.
//

#include <gtest/gtest.h>
#include "StringFormat.h" // StringFormat 헤더 파일 포함



class StringFormatTest : public ::testing::Test {
protected:
    // 테스트에 사용할 데이터를 초기화하는 부분
    void SetUp() override {}

    // 테스트 후 정리하는 부분
    void TearDown() override {}
};



// Trim 함수 테스트
TEST_F(StringFormatTest, Trim) {
    EXPECT_EQ(NebulaStringFormat::Trim(std::string("  Hello  ")), "Hello");
    EXPECT_EQ(NebulaStringFormat::Trim(std::string("Hello")), "Hello");
    EXPECT_EQ(NebulaStringFormat::Trim(std::string("  ")), "");
}

// TrimInPlace 함수 테스트
TEST_F(StringFormatTest, TrimInPlace) {
    std::string str = "  Hello  ";
    EXPECT_EQ(NebulaStringFormat::TrimInPlace(str), "Hello");
    EXPECT_EQ(str, "Hello");

    str = "Hello";
    EXPECT_EQ(NebulaStringFormat::TrimInPlace(str), "Hello");
    EXPECT_EQ(str, "Hello");

    str = "  ";
    EXPECT_EQ(NebulaStringFormat::TrimInPlace(str), "");
    EXPECT_EQ(str, "");
}

// TrimLeft 함수 테스트
TEST_F(StringFormatTest, TrimLeft) {
    EXPECT_EQ(NebulaStringFormat::TrimLeft(std::string("  Hello  ")), "Hello  ");
    EXPECT_EQ(NebulaStringFormat::TrimLeft(std::string("Hello")), "Hello");
    EXPECT_EQ(NebulaStringFormat::TrimLeft(std::string("  ")), "");
}

// TrimLeftInPlace 함수 테스트
TEST_F(StringFormatTest, TrimLeftInPlace) {
    std::string str = "  Hello  ";
    EXPECT_EQ(NebulaStringFormat::TrimLeftInPlace(str), "Hello  ");
    EXPECT_EQ(str, "Hello  ");

    str = "Hello";
    EXPECT_EQ(NebulaStringFormat::TrimLeftInPlace(str), "Hello");
    EXPECT_EQ(str, "Hello");

    str = "  ";
    EXPECT_EQ(NebulaStringFormat::TrimLeftInPlace(str), "");
    EXPECT_EQ(str, "");
}

// TrimRight 함수 테스트
TEST_F(StringFormatTest, TrimRight) {
    EXPECT_EQ(NebulaStringFormat::TrimRight(std::string("  Hello  ")), "  Hello");
    EXPECT_EQ(NebulaStringFormat::TrimRight(std::string("Hello")), "Hello");
    EXPECT_EQ(NebulaStringFormat::TrimRight(std::string("  ")), "");
}

// TrimRightInPlace 함수 테스트
TEST_F(StringFormatTest, TrimRightInPlace) {
    std::string str = "  Hello  ";
    EXPECT_EQ(NebulaStringFormat::TrimRightInPlace(str), "  Hello");
    EXPECT_EQ(str, "  Hello");

    str = "Hello";
    EXPECT_EQ(NebulaStringFormat::TrimRightInPlace(str), "Hello");
    EXPECT_EQ(str, "Hello");

    str = "  ";
    EXPECT_EQ(NebulaStringFormat::TrimRightInPlace(str), "");
    EXPECT_EQ(str, "");
}

// Lower 함수 테스트
TEST_F(StringFormatTest, Lower) {
    EXPECT_EQ(NebulaStringFormat::Lower(std::string("Hello")), "hello");
    EXPECT_EQ(NebulaStringFormat::Lower(std::string("WORLD")), "world");
    EXPECT_EQ(NebulaStringFormat::Lower(std::string("123")), "123");
}

// LowerInPlace 함수 테스트
TEST_F(StringFormatTest, LowerInPlace) {
    std::string str = "Hello";
    EXPECT_EQ(NebulaStringFormat::LowerInPlace(str), "hello");
    EXPECT_EQ(str, "hello");

    str = "WORLD";
    EXPECT_EQ(NebulaStringFormat::LowerInPlace(str), "world");
    EXPECT_EQ(str, "world");

    str = "123";
    EXPECT_EQ(NebulaStringFormat::LowerInPlace(str), "123");
    EXPECT_EQ(str, "123");
}

// Upper 함수 테스트
TEST_F(StringFormatTest, Upper) {
    EXPECT_EQ(NebulaStringFormat::Upper(std::string("Hello")), "HELLO");
    EXPECT_EQ(NebulaStringFormat::Upper(std::string("world")), "WORLD");
    EXPECT_EQ(NebulaStringFormat::Upper(std::string("123")), "123");
}

// UpperInPlace 함수 테스트
TEST_F(StringFormatTest, UpperInPlace) {
    std::string str = "Hello";
    EXPECT_EQ(NebulaStringFormat::UpperInPlace(str), "HELLO");
    EXPECT_EQ(str, "HELLO");

    str = "world";
    EXPECT_EQ(NebulaStringFormat::UpperInPlace(str), "WORLD");
    EXPECT_EQ(str, "WORLD");

    str = "123";
    EXPECT_EQ(NebulaStringFormat::UpperInPlace(str), "123");
    EXPECT_EQ(str, "123");
}

// Replace 함수 테스트
TEST_F(StringFormatTest, Replace) {
    std::string str("Hello World");

    EXPECT_EQ(NebulaStringFormat::Replace(str, std::string("World"), std::string("C++")), "Hello C++");
    EXPECT_EQ(NebulaStringFormat::Replace(str, std::string("Hello"), std::string("Hi")), "Hi World");
    EXPECT_EQ(NebulaStringFormat::Replace(str, std::string("Earth"), std::string("Mars")), "Hello World");
}

// ReplaceInPlace 함수 테스트
TEST_F(StringFormatTest, ReplaceInPlace) {
    std::string str = "Hello World";
    EXPECT_EQ(NebulaStringFormat::ReplaceInPlace(str, "World", "C++"), "Hello C++");
    EXPECT_EQ(str, "Hello C++");

    str = "Hello World";
    EXPECT_EQ(NebulaStringFormat::ReplaceInPlace(str, "Hello", "Hi"), "Hi World");
    EXPECT_EQ(str, "Hi World");

    str = "Hello World";
    EXPECT_EQ(NebulaStringFormat::ReplaceInPlace(str, "Earth", "Mars"), "Hello World"); // 대체할 문자열이 없을 때
}

// Compare 함수 테스트
TEST_F(StringFormatTest, Compare) {
    EXPECT_EQ(NebulaStringFormat::Compare(std::string("Hello"), std::string("Hello")), 0);
    EXPECT_EQ(NebulaStringFormat::Compare(std::string("Hello"), std::string("World")), -1);
    EXPECT_EQ(NebulaStringFormat::Compare(std::string("World"), std::string("Hello")), 1);
}

// CompareIgnoreCase 함수 테스트
TEST_F(StringFormatTest, CompareIgnoreCase) {
    EXPECT_EQ(NebulaStringFormat::CompareIgnoreCase(std::string("Hello"), std::string("hello")), 0);
    EXPECT_EQ(NebulaStringFormat::CompareIgnoreCase(std::string("Hello"), std::string("World")), -1);
    EXPECT_EQ(NebulaStringFormat::CompareIgnoreCase(std::string("World"), std::string("Hello")), 1);
}

// Tokenize 함수 테스트
TEST_F(StringFormatTest, Tokenize) {
    std::vector<std::string> tokens;
    EXPECT_TRUE(NebulaStringFormat::Tokenize(std::string("Hello,World,This,is,a,test"), std::string(","), tokens));
    EXPECT_EQ(tokens.size(), 6);
    EXPECT_EQ(tokens[0], "Hello");
    EXPECT_EQ(tokens[1], "World");
    EXPECT_EQ(tokens[2], "This");
    EXPECT_EQ(tokens[3], "is");
    EXPECT_EQ(tokens[4], "a");
    EXPECT_EQ(tokens[5], "test");
}

// EqualCaseInsensitive 함수 테스트
TEST_F(StringFormatTest, EqualCaseInsensitive) {
    EXPECT_TRUE(NebulaStringFormat::EqualCaseInsensitive("Hello", "hello"));
    EXPECT_FALSE(NebulaStringFormat::EqualCaseInsensitive("Hello", "world"));
}

// WCStoMBCS 함수 테스트
#if defined(_WIN32)
TEST_F(StringFormatTest, WCStoMBCS) {
    EXPECT_EQ(NebulaStringFormat::WCStoMBCS(L"Hello"), "Hello");
}

// WCStoUTF8 함수 테스트
TEST_F(StringFormatTest, WCStoUTF8) {
    EXPECT_EQ(NebulaStringFormat::WCStoUTF8(L"Hello"), "Hello");
}

// MBCStoUTF8 함수 테스트
TEST_F(StringFormatTest, MBCStoUTF8) {
    EXPECT_EQ(NebulaStringFormat::MBCStoUTF8("Hello"), "Hello");
}

// MBCStoWCS 함수 테스트
TEST_F(StringFormatTest, MBCStoWCS) {
    EXPECT_EQ(NebulaStringFormat::MBCStoWCS("Hello"), L"Hello");
}

// UTF8toMBCS 함수 테스트
TEST_F(StringFormatTest, UTF8toMBCS) {
    EXPECT_EQ(NebulaStringFormat::UTF8toMBCS("Hello"), "Hello");
}

// UTF8toWCS 함수 테스트
TEST_F(StringFormatTest, UTF8toWCS) {
    EXPECT_EQ(NebulaStringFormat::UTF8toWCS("Hello"), L"Hello");
}
#endif
