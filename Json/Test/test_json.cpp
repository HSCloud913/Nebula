//
// Created by nebula on 24. 11. 3.
//

#include <gtest/gtest.h>
#include "Json/Json.h"



class NebulaJsonValueTest :public ::testing::Test
{
protected:
	ne::json::Value jsonValue; // 테스트를 위한 JsonValue 객체
};



// Json 직렬화
TEST_F(NebulaJsonValueTest, SerializeJson)
{
	ne::json::Object object;
	object["bool_value"] = false;
	object["number_value"] = -1;
	object["positive_number_value"] = 4294967295U;
	object["large_number_value"] = 9223372036854775807LL;
	object["positive_large_number_value"] = 18446744073709551615ULL;
	object["real_value"] = 0.123456789f;
	object["string_value"] = "Test string";

	auto string = ne::json::Stringify(object);
	EXPECT_EQ(string,
			R"({"bool_value":false,"large_number_value":9223372036854775807,"number_value":-1,"positive_large_number_value":18446744073709551615,"positive_number_value":4294967295,"real_value":0.123456791043282,"string_value":"Test string"})");
}

// 유효한 JSON parse
TEST_F(NebulaJsonValueTest, ParseValidJson)
{
	auto root = ne::json::Parse(R"({"key":"value"})");
	EXPECT_TRUE(root.IsObject());

	auto object = root.AsObject();
	EXPECT_EQ(*object["key"].AsString(), "value");
}

// TryAs*(): 타입 일치 → Ok, 불일치 → Err (예외 없이 Result 로 표현)
TEST_F(NebulaJsonValueTest, TryAsReturnsResult)
{
	auto root = ne::json::Parse(R"({"n":-1,"s":"hi"})");
	ASSERT_TRUE(root.IsObject());
	const auto& obj = root.AsObject();

	const auto n = obj.at("n").TryAsNumber();
	ASSERT_TRUE(n.IsOk());
	EXPECT_EQ(n.Value(), -1);

	// 타입 불일치: 숫자를 bool 로 요청 → Err (throw 없음)
	EXPECT_TRUE(obj.at("n").TryAsBool().IsError());

	// 문자열/객체/배열은 포인터 Result
	const auto s = obj.at("s").TryAsString();
	ASSERT_TRUE(s.IsOk());
	EXPECT_EQ(*s.Value(), "hi");

	const auto o = root.TryAsObject();
	ASSERT_TRUE(o.IsOk());
	EXPECT_NE(o.Value(), nullptr);

	EXPECT_TRUE(root.TryAsArray().IsError()); // object 를 array 로 요청
}

// TryParse(): 성공 시 Ok, 실패 시 위치(offset)+사유를 담은 Err
TEST_F(NebulaJsonValueTest, TryParseReportsError)
{
	EXPECT_TRUE(ne::json::TryParse(R"({"k":1})").IsOk());

	const auto empty = ne::json::TryParse("   ");
	ASSERT_TRUE(empty.IsError());
	EXPECT_FALSE(empty.Error().message.empty());

	const auto bad = ne::json::TryParse("@");
	ASSERT_TRUE(bad.IsError());
	EXPECT_EQ(bad.Error().position, 0u);

	const auto trailing = ne::json::TryParse("1 2");
	ASSERT_TRUE(trailing.IsError());
	EXPECT_EQ(trailing.Error().position, 2u);

	EXPECT_TRUE(ne::json::TryParse(R"({"k":1)").IsError()); // 미완결(EOF)
}

// 잘못된 JSON parse
TEST_F(NebulaJsonValueTest, ParseInvalidJson)
{
	auto root = ne::json::Parse(R"({"key":"value")");
	EXPECT_FALSE(root.IsObject());
}

// JSON 직렬화 후 다시 파싱
TEST_F(NebulaJsonValueTest, SerializeAndParseJson)
{
	ne::json::Object object;
	object["bool_value"] = false;
	object["number_value"] = -1;
	object["positive_number_value"] = 4294967295U;
	object["large_number_value"] = 9223372036854775807LL;
	object["positive_large_number_value"] = 18446744073709551615ULL;
	object["real_value"] = 0.123456789f;
	object["string_value"] = "Test string";

	auto string = ne::json::Stringify(object);
	EXPECT_EQ(string,
			R"({"bool_value":false,"large_number_value":9223372036854775807,"number_value":-1,"positive_large_number_value":18446744073709551615,"positive_number_value":4294967295,"real_value":0.123456791043282,"string_value":"Test string"})");

	auto root = ne::json::Parse(string.c_str());
	EXPECT_TRUE(root.IsObject());

	ne::json::Object parseObject = root.AsObject();
	EXPECT_EQ(parseObject["bool_value"].AsBool(), false);
	EXPECT_EQ(parseObject["number_value"].AsNumber(), -1);
	EXPECT_EQ(parseObject["positive_number_value"].AsPositiveNumber(), 4294967295U);
	EXPECT_EQ(parseObject["large_number_value"].AsLargeNumber(), 9223372036854775807LL);
	EXPECT_EQ(parseObject["positive_large_number_value"].AsPositiveLargeNumber(), 18446744073709551615ULL);
	EXPECT_NEAR(parseObject["real_value"].AsReal(), 0.123456789f, 1e-9);
	EXPECT_EQ(*parseObject["string_value"].AsString(), "Test string");
}

// 소수점 없는 지수 표기법 파싱 (예: 3e2 = 300.0)
TEST_F(NebulaJsonValueTest, ParseIntegerExponent)
{
	auto v1 = ne::json::Parse("3e2");
	EXPECT_TRUE(v1.IsReal());
	EXPECT_NEAR(v1.AsReal(), 300.0, 1e-9);

	auto v2 = ne::json::Parse("5e3");
	EXPECT_TRUE(v2.IsReal());
	EXPECT_NEAR(v2.AsReal(), 5000.0, 1e-9);

	auto v3 = ne::json::Parse("2e-1");
	EXPECT_TRUE(v3.IsReal());
	EXPECT_NEAR(v3.AsReal(), 0.2, 1e-9);
}

// JSON 객체에 값 추가
TEST_F(NebulaJsonValueTest, AddValue)
{
	ne::json::Object subObject;
	subObject["sub1"] = true;
	subObject["sub2"] = 2;
	subObject["sub3"] = 0.12f;
	subObject["sub4"] = "Test sub string";

	ne::json::Array subArray;
	for (ne::int_t i = 0; i < 3; i++) { subArray.emplace_back(subObject); }

	ne::json::Object object;
	object["bool_value"] = false;
	object["number_value"] = -1;
	object["positive_number_value"] = 4294967295U;
	object["large_number_value"] = 9223372036854775807LL;
	object["positive_large_number_value"] = 18446744073709551615ULL;
	object["real_value"] = 0.123456789f;
	object["string_value"] = "Test string";
	object["object_value"] = subObject;
	object["array_value"] = subArray;

	EXPECT_EQ(object["bool_value"].AsBool(), false);
	EXPECT_EQ(object["number_value"].AsNumber(), -1);
	EXPECT_EQ(object["positive_number_value"].AsPositiveNumber(), 4294967295U);
	EXPECT_EQ(object["large_number_value"].AsLargeNumber(), 9223372036854775807LL);
	EXPECT_EQ(object["positive_large_number_value"].AsPositiveLargeNumber(), 18446744073709551615ULL);
	EXPECT_EQ(object["real_value"].AsReal(), 0.123456789f);
	EXPECT_EQ(*object["string_value"].AsString(), "Test string");

	auto& object_value = object["object_value"].AsObject();
	EXPECT_EQ(object_value.at("sub1").AsBool(), true);
	EXPECT_EQ(object_value.at("sub2").AsNumber(), 2);
	EXPECT_EQ(object_value.at("sub3").AsReal(), 0.12f);
	EXPECT_EQ(*object_value.at("sub4").AsString(), "Test sub string");

	EXPECT_EQ(subArray.size(), 3); // 배열 크기 확인
	for (auto i = 0; i < subArray.size(); ++i)
	{
		auto& testObject = subArray[i].AsObject();
		EXPECT_EQ(testObject.at("sub1").AsBool(), true);
		EXPECT_EQ(testObject.at("sub2").AsNumber(), 2);
		EXPECT_EQ(testObject.at("sub3").AsReal(), 0.12f);
		EXPECT_EQ(*testObject.at("sub4").AsString(), "Test sub string");
	}
}

// JSON 객체에 존재하는 값 제거
TEST_F(NebulaJsonValueTest, RemoveValue)
{
	ne::json::Object object;
	object["bool_value"] = false;
	EXPECT_EQ(object["bool_value"].AsBool(), false);

	object.erase("bool_value");
	EXPECT_EQ(object.size(), 0);
}

// 중복된 key
TEST_F(NebulaJsonValueTest, DuplicateKeys)
{
	ne::json::Object object;
	object["bool_value"] = false;
	object["bool_value"] = true;

	EXPECT_EQ(object["bool_value"].AsBool(), true);
}

// 빈 문자열 확인
TEST_F(NebulaJsonValueTest, Whitespace)
{
	ne::lpcstr_t EXAMPLE = "                           {             \
 	\"string_name\"     :    \"asdf\", \
 	\"bool_name\"     :     true, \
 	\"bool_second\"   :   TrUe, \
 	\"null_name\"  :   nULl, \
	\"nua\"  :  9223372036854775807, \
	\"max\"  :  18446744073709551615, \
	\"oor\"  :  18446744073709551616, \
 	\"negative\"  :  -34.276, \
 	\"sub_object\"  :  { \
 						\"foo\"   :   \"abc\", \
 						 \"bar\"   :   1.35e2, \
 						 \"blah\"   :   { \"a\" : \"A\", \"b\" : \"B\", \"c\" : \"C\"            } \
 					}, \
 	\"array_letters\" : [ \"a\", \"b\", \"c\", [ 1, 2, 3  ]  ] \
 }                                                           ";

	auto root = ne::json::Parse(EXAMPLE);
	EXPECT_TRUE(root.IsObject());

	ne::json::Object parseObject = root.AsObject();
	EXPECT_EQ(*parseObject["string_name"].AsString(), "asdf");
	EXPECT_EQ(parseObject["bool_name"].AsBool(), true);
	EXPECT_EQ(parseObject["bool_second"].AsBool(), true);
	EXPECT_TRUE(parseObject["null_name"].IsNull());
	EXPECT_EQ(parseObject["nua"].AsLargeNumber(), 9223372036854775807LL);
	EXPECT_EQ(parseObject["max"].AsPositiveLargeNumber(), 18446744073709551615ULL);
	EXPECT_FALSE(parseObject.contains("oor")); // 표현 범위 초과 값은 INVALID → 객체에 추가되지 않음
	EXPECT_NEAR(parseObject["negative"].AsReal(), -34.276f, 1e-3);

	auto& subObject = parseObject["sub_object"].AsObject();
	EXPECT_EQ(*subObject.at("foo").AsString(), "abc");
	EXPECT_EQ(subObject.at("bar").AsReal(), 1.35e2);

	auto blahObject = subObject.at("blah").AsObject();
	EXPECT_EQ(*blahObject.at("a").AsString(), "A");
	EXPECT_EQ(*blahObject.at("b").AsString(), "B");
	EXPECT_EQ(*blahObject.at("c").AsString(), "C");

	auto& arrayLetters = parseObject["array_letters"].AsArray();

	EXPECT_EQ(arrayLetters.size(), 4);
	EXPECT_EQ(*arrayLetters[0].AsString(), "a");
	EXPECT_EQ(*arrayLetters[1].AsString(), "b");
	EXPECT_EQ(*arrayLetters[2].AsString(), "c");

	auto& arraySubLetters = arrayLetters[3].AsArray();
	EXPECT_EQ(arraySubLetters[0].AsNumber(), 1);
	EXPECT_EQ(arraySubLetters[1].AsNumber(), 2);
	EXPECT_EQ(arraySubLetters[2].AsNumber(), 3);
}
