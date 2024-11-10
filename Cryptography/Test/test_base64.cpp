//
// Created by hsclo on 24. 11. 10.
//

#include <gtest/gtest.h>
#include <Base64/Base64.h>



TEST(Base64Test, Base64Encoding)
{
	auto result = NebulaBase64::Encode("123456789");

	EXPECT_EQ(result, "MTIzNDU2Nzg5");
}