//
// Created by nebula on 24. 11. 10.
//

#include <gtest/gtest.h>
#include <Base64/Base64.h>



namespace crypto = ne::cryptography;

TEST(Base64Test, Encode)
{
	EXPECT_EQ(crypto::Base64::Encode("123456789"), "MTIzNDU2Nzg5");
	EXPECT_EQ(crypto::Base64::Encode("Hello, World!"), "SGVsbG8sIFdvcmxkIQ==");
}

TEST(Base64Test, EncodeBuffer)
{
	ne::char_t buffer[64] = { 0, };
	crypto::Base64::Encode("123456789", buffer, sizeof(buffer));
	EXPECT_STREQ(buffer, "MTIzNDU2Nzg5");
}

TEST(Base64Test, Decode)
{
	EXPECT_EQ(crypto::Base64::Decode("MTIzNDU2Nzg5"), "123456789");
	EXPECT_EQ(crypto::Base64::Decode("SGVsbG8sIFdvcmxkIQ=="), "Hello, World!");
}

TEST(Base64Test, DecodeBuffer)
{
	ne::char_t buffer[64] = { 0, };
	crypto::Base64::Decode("MTIzNDU2Nzg5", buffer, sizeof(buffer));
	EXPECT_STREQ(buffer, "123456789");
}

TEST(Base64Test, EncodeURL)
{
	// '+' -> '-', '/' -> '_', padding removed
	EXPECT_EQ(crypto::Base64::EncodeURL(ne::string_t("\xfb\xff\xfe", 3)), "-__-");
	EXPECT_EQ(crypto::Base64::EncodeURL("Hello, World!"), "SGVsbG8sIFdvcmxkIQ");
}

TEST(Base64Test, DecodeURL)
{
	EXPECT_EQ(crypto::Base64::DecodeURL("-__-"), ne::string_t("\xfb\xff\xfe", 3));
	EXPECT_EQ(crypto::Base64::DecodeURL("SGVsbG8sIFdvcmxkIQ"), "Hello, World!");
}
