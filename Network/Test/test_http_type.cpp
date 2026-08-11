//
// Created by hscloud on 26. 7. 21.
//
// Http Message/(Method·Body·Request·Response) 의 순수 로직(네트워크 불필요) 단위 테스트.

#include <gtest/gtest.h>
#include <cstring>
#include <vector>
#include "Network/Protocol/Http/Message/Request.h"
#include "Network/Protocol/Http/Message/Response.h"

using namespace ne::network::http;

TEST(HeadersTest, GetIsCaseInsensitive)
{
	Headers headers;
	headers.Add("Content-Type", "text/plain");

	EXPECT_EQ(headers.Get("content-type"), "text/plain");
	EXPECT_EQ(headers.Get("CONTENT-TYPE"), "text/plain");
	EXPECT_TRUE(headers.Has("Content-Type"));
	EXPECT_TRUE(headers.Has("content-type"));
}

TEST(HeadersTest, SetReplacesAllExistingValuesForName)
{
	Headers headers;
	headers.Add("X-Trace", "first");
	headers.Add("X-Trace", "second");
	ASSERT_EQ(headers.GetAll("X-Trace").size(), 2u);

	headers.Set("X-Trace", "replaced");

	const auto all = headers.GetAll("X-Trace");
	ASSERT_EQ(all.size(), 1u);
	EXPECT_EQ(all[0], "replaced");
}

TEST(HeadersTest, GetAllReturnsEveryValueForDuplicateName)
{
	Headers headers;
	headers.Add("Set-Cookie", "a=1");
	headers.Add("Set-Cookie", "b=2");

	const auto all = headers.GetAll("Set-Cookie");
	ASSERT_EQ(all.size(), 2u);
	EXPECT_EQ(all[0], "a=1");
	EXPECT_EQ(all[1], "b=2");
}

TEST(HeadersTest, InitializerListConstructorAddsAllEntries)
{
	const Headers headers{ { "Authorization", "Bearer x" }, { "X-Custom", "y" } };

	EXPECT_EQ(headers.Get("authorization"), "Bearer x");
	EXPECT_EQ(headers.Get("x-custom"), "y");
}

TEST(HeadersTest, GetMissingNameReturnsNullopt)
{
	const Headers headers;
	EXPECT_FALSE(headers.Get("Nowhere").has_value());
	EXPECT_FALSE(headers.Has("Nowhere"));
}



TEST(BodyTest, DefaultIsEmpty)
{
	const Body body;
	EXPECT_TRUE(body.IsEmpty());
	EXPECT_EQ(body.Size(), 0u);
}

TEST(BodyTest, FromStringOwnsCopyAndReportsCorrectSize)
{
	const auto body = Body::FromString("hello");

	EXPECT_FALSE(body.IsEmpty());
	EXPECT_EQ(body.Size(), 5u);

	const auto view = body.View();
	ASSERT_EQ(view.TotalSize(), 5u);
}

TEST(BodyTest, ViewOfOwnedVectorReflectsBytes)
{
	const std::vector<ne::byte_t> bytes{ 'a', 'b', 'c' };
	const Body body(bytes);

	const auto view = body.View();
	ASSERT_EQ(view.Segments().size(), 1u);
	EXPECT_EQ(view.Segments()[0].length, 3u);
	EXPECT_EQ(std::memcmp(view.Segments()[0].ptr, bytes.data(), 3), 0);
}



TEST(HttpTypeTest, MethodRoundTripsThroughString)
{
	EXPECT_EQ(ToString(Method::GET), "GET");
	EXPECT_EQ(ToString(Method::POST), "POST");
	EXPECT_EQ(ToString(Method::DELETE_), "DELETE");

	EXPECT_EQ(MethodFromString("GET"), Method::GET);
	EXPECT_EQ(MethodFromString("DELETE"), Method::DELETE_);
	EXPECT_EQ(MethodFromString("NOSUCHMETHOD"), Method::UNKNOWN);
}

TEST(HttpTypeTest, DefaultReasonPhraseCoversCommonCodes)
{
	EXPECT_EQ(DefaultReasonPhrase(200), "OK");
	EXPECT_EQ(DefaultReasonPhrase(404), "Not Found");
	EXPECT_EQ(DefaultReasonPhrase(500), "Internal Server Error");
	EXPECT_EQ(DefaultReasonPhrase(999), "");
}



TEST(ResponseTest, TextFactorySetsContentTypeAndBody)
{
	const auto response = Response::Text(200, "hello world");

	EXPECT_EQ(response.statusCode, 200);
	EXPECT_EQ(response.headers.Get("Content-Type"), "text/plain; charset=utf-8");
	EXPECT_EQ(response.body.Size(), 11u);
}

TEST(ResponseTest, JsonFactorySetsContentTypeAndBody)
{
	const auto response = Response::Json(201, R"({"ok":true})");

	EXPECT_EQ(response.statusCode, 201);
	EXPECT_EQ(response.headers.Get("Content-Type"), "application/json");
	EXPECT_FALSE(response.body.IsEmpty());
}

TEST(ResponseTest, StatusFactoryHasNoBody)
{
	const auto response = Response::Status(404);

	EXPECT_EQ(response.statusCode, 404);
	EXPECT_TRUE(response.body.IsEmpty());
	EXPECT_TRUE(response.reason.empty()); // 직렬화 시점에 DefaultReasonPhrase 로 채워짐(SerializeStatusLine)
}
