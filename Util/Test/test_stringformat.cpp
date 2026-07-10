//
// Created by nebula on 24. 10. 21.
//

#include <gtest/gtest.h>
#include "Util/StringFormat.h" // StringFormat 헤더 파일 포함



class StringFormatTest :public ::testing::Test
{
protected:
	// 테스트에 사용할 데이터를 초기화하는 부분
	void SetUp() override {}

	// 테스트 후 정리하는 부분
	void TearDown() override {}
};



// Trim 함수 테스트
TEST_F(StringFormatTest, Trim)
{
	EXPECT_EQ(ne::StringFormat::Trim(std::string("  Hello  ")), "Hello");
	EXPECT_EQ(ne::StringFormat::Trim(std::string("Hello")), "Hello");
	EXPECT_EQ(ne::StringFormat::Trim(std::string("  ")), "");
}

// TrimInPlace 함수 테스트
TEST_F(StringFormatTest, TrimInPlace)
{
	std::string str = "  Hello  ";
	EXPECT_EQ(ne::StringFormat::TrimInPlace(str), "Hello");
	EXPECT_EQ(str, "Hello");

	str = "Hello";
	EXPECT_EQ(ne::StringFormat::TrimInPlace(str), "Hello");
	EXPECT_EQ(str, "Hello");

	str = "  ";
	EXPECT_EQ(ne::StringFormat::TrimInPlace(str), "");
	EXPECT_EQ(str, "");
}

// TrimLeft 함수 테스트
TEST_F(StringFormatTest, TrimLeft)
{
	EXPECT_EQ(ne::StringFormat::TrimLeft(std::string("  Hello  ")), "Hello  ");
	EXPECT_EQ(ne::StringFormat::TrimLeft(std::string("Hello")), "Hello");
	EXPECT_EQ(ne::StringFormat::TrimLeft(std::string("  ")), "");
}

// TrimLeftInPlace 함수 테스트
TEST_F(StringFormatTest, TrimLeftInPlace)
{
	std::string str = "  Hello  ";
	EXPECT_EQ(ne::StringFormat::TrimLeftInPlace(str), "Hello  ");
	EXPECT_EQ(str, "Hello  ");

	str = "Hello";
	EXPECT_EQ(ne::StringFormat::TrimLeftInPlace(str), "Hello");
	EXPECT_EQ(str, "Hello");

	str = "  ";
	EXPECT_EQ(ne::StringFormat::TrimLeftInPlace(str), "");
	EXPECT_EQ(str, "");
}

// TrimRight 함수 테스트
TEST_F(StringFormatTest, TrimRight)
{
	EXPECT_EQ(ne::StringFormat::TrimRight(std::string("  Hello  ")), "  Hello");
	EXPECT_EQ(ne::StringFormat::TrimRight(std::string("Hello")), "Hello");
	EXPECT_EQ(ne::StringFormat::TrimRight(std::string("  ")), "");
}

// TrimRightInPlace 함수 테스트
TEST_F(StringFormatTest, TrimRightInPlace)
{
	std::string str = "  Hello  ";
	EXPECT_EQ(ne::StringFormat::TrimRightInPlace(str), "  Hello");
	EXPECT_EQ(str, "  Hello");

	str = "Hello";
	EXPECT_EQ(ne::StringFormat::TrimRightInPlace(str), "Hello");
	EXPECT_EQ(str, "Hello");

	str = "  ";
	EXPECT_EQ(ne::StringFormat::TrimRightInPlace(str), "");
	EXPECT_EQ(str, "");
}

// Lower 함수 테스트
TEST_F(StringFormatTest, Lower)
{
	EXPECT_EQ(ne::StringFormat::Lower(std::string("Hello")), "hello");
	EXPECT_EQ(ne::StringFormat::Lower(std::string("WORLD")), "world");
	EXPECT_EQ(ne::StringFormat::Lower(std::string("123")), "123");
}

// LowerInPlace 함수 테스트
TEST_F(StringFormatTest, LowerInPlace)
{
	std::string str = "Hello";
	EXPECT_EQ(ne::StringFormat::LowerInPlace(str), "hello");
	EXPECT_EQ(str, "hello");

	str = "WORLD";
	EXPECT_EQ(ne::StringFormat::LowerInPlace(str), "world");
	EXPECT_EQ(str, "world");

	str = "123";
	EXPECT_EQ(ne::StringFormat::LowerInPlace(str), "123");
	EXPECT_EQ(str, "123");
}

// Upper 함수 테스트
TEST_F(StringFormatTest, Upper)
{
	EXPECT_EQ(ne::StringFormat::Upper(std::string("Hello")), "HELLO");
	EXPECT_EQ(ne::StringFormat::Upper(std::string("world")), "WORLD");
	EXPECT_EQ(ne::StringFormat::Upper(std::string("123")), "123");
}

// UpperInPlace 함수 테스트
TEST_F(StringFormatTest, UpperInPlace)
{
	std::string str = "Hello";
	EXPECT_EQ(ne::StringFormat::UpperInPlace(str), "HELLO");
	EXPECT_EQ(str, "HELLO");

	str = "world";
	EXPECT_EQ(ne::StringFormat::UpperInPlace(str), "WORLD");
	EXPECT_EQ(str, "WORLD");

	str = "123";
	EXPECT_EQ(ne::StringFormat::UpperInPlace(str), "123");
	EXPECT_EQ(str, "123");
}

// Replace 함수 테스트
TEST_F(StringFormatTest, Replace)
{
	std::string str("Hello World");

	EXPECT_EQ(ne::StringFormat::Replace(str, std::string("World"), std::string("C++")), "Hello C++");
	EXPECT_EQ(ne::StringFormat::Replace(str, std::string("Hello"), std::string("Hi")), "Hi World");
	EXPECT_EQ(ne::StringFormat::Replace(str, std::string("Earth"), std::string("Mars")), "Hello World");
}

// ReplaceInPlace 함수 테스트
TEST_F(StringFormatTest, ReplaceInPlace)
{
	std::string str = "Hello World";
	EXPECT_EQ(ne::StringFormat::ReplaceInPlace(str, "World", "C++"), "Hello C++");
	EXPECT_EQ(str, "Hello C++");

	str = "Hello World";
	EXPECT_EQ(ne::StringFormat::ReplaceInPlace(str, "Hello", "Hi"), "Hi World");
	EXPECT_EQ(str, "Hi World");

	str = "Hello World";
	EXPECT_EQ(ne::StringFormat::ReplaceInPlace(str, "Earth", "Mars"), "Hello World"); // 대체할 문자열이 없을 때
}

// Compare 함수 테스트
TEST_F(StringFormatTest, Compare)
{
	EXPECT_EQ(ne::StringFormat::Compare(std::string("Hello"), std::string("Hello")), 0);
	EXPECT_LT(ne::StringFormat::Compare(std::string("Hello"), std::string("World")), 0);
	EXPECT_GT(ne::StringFormat::Compare(std::string("World"), std::string("Hello")), 0);
}

// CompareIgnoreCase 함수 테스트
TEST_F(StringFormatTest, CompareIgnoreCase)
{
	EXPECT_EQ(ne::StringFormat::CompareIgnoreCase(std::string("Hello"), std::string("hello")), 0);
	EXPECT_LT(ne::StringFormat::CompareIgnoreCase(std::string("Hello"), std::string("World")), 0);
	EXPECT_GT(ne::StringFormat::CompareIgnoreCase(std::string("World"), std::string("Hello")), 0);
}

// Tokenize 함수 테스트
TEST_F(StringFormatTest, Tokenize)
{
	std::vector<std::string> tokens;
	EXPECT_TRUE(ne::StringFormat::Tokenize(std::string("Hello,World,This,is,a,test"), std::string(","), tokens));
	EXPECT_EQ(tokens.size(), 6);
	EXPECT_EQ(tokens[0], "Hello");
	EXPECT_EQ(tokens[1], "World");
	EXPECT_EQ(tokens[2], "This");
	EXPECT_EQ(tokens[3], "is");
	EXPECT_EQ(tokens[4], "a");
	EXPECT_EQ(tokens[5], "test");
}

// EqualCaseInsensitive 함수 테스트
TEST_F(StringFormatTest, EqualCaseInsensitive)
{
	EXPECT_TRUE(ne::StringFormat::EqualCaseInsensitive("Hello", "hello"));
	EXPECT_FALSE(ne::StringFormat::EqualCaseInsensitive("Hello", "world"));
}

// WCStoMBCS 함수 테스트
#if defined(_WIN32)
TEST_F(StringFormatTest, WCStoMBCS) { EXPECT_EQ(ne::StringFormat::WCStoMBCS(L"Hello"), "Hello"); }

// WCStoUTF8 함수 테스트
TEST_F(StringFormatTest, WCStoUTF8) { EXPECT_EQ(ne::StringFormat::WCStoUTF8(L"Hello"), "Hello"); }

// MBCStoUTF8 함수 테스트
TEST_F(StringFormatTest, MBCStoUTF8) { EXPECT_EQ(ne::StringFormat::MBCStoUTF8("Hello"), "Hello"); }

// MBCStoWCS 함수 테스트
TEST_F(StringFormatTest, MBCStoWCS) { EXPECT_EQ(ne::StringFormat::MBCStoWCS("Hello"), L"Hello"); }

// UTF8toMBCS 함수 테스트
TEST_F(StringFormatTest, UTF8toMBCS) { EXPECT_EQ(ne::StringFormat::UTF8toMBCS("Hello"), "Hello"); }

// UTF8toWCS 함수 테스트
TEST_F(StringFormatTest, UTF8toWCS) { EXPECT_EQ(ne::StringFormat::UTF8toWCS("Hello"), L"Hello"); }
#endif
