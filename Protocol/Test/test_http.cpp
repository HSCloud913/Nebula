//
// Created by nebula on 24. 11. 3.
//

#include <gtest/gtest.h>
#include <chrono>
#include <string>

#include "Exception.h"
#include "Http/Client/Response.h"
#include "Http/Client/Request.h"
#include "Http/Server/Response.h"
#include "Http/Server/Server.h"

using namespace ne::protocol::Http;
using ne::protocol::Http::Client::Parser;
using ne::protocol::Http::Client::ChunkBodyParser;

namespace
{
	std::span<const std::byte> AsBytes(const std::string& _string)
	{
		return std::span(reinterpret_cast<const std::byte*>(_string.data()), _string.size());
	}

	std::string AsString(const std::span<const std::byte> _bytes)
	{
		return std::string(reinterpret_cast<const char*>(_bytes.data()), _bytes.size());
	}
}

TEST(HttpParserTest, ParsesHeadRequestMethodCorrectly)
{
	auto parser = Parser();
	const auto request = std::string("HEAD /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n");

	const auto result = parser.Parse(AsBytes(request));
	ASSERT_TRUE(result);
	EXPECT_EQ(result->method, Method::HEAD);
	EXPECT_EQ(result->path, "/index.html");
}

TEST(HttpParserTest, ParsesGetRequestMethodCorrectly)
{
	auto parser = Parser();
	const auto request = std::string("GET /search?q=test HTTP/1.1\r\nHost: example.com\r\n\r\n");

	const auto result = parser.Parse(AsBytes(request));
	ASSERT_TRUE(result);
	EXPECT_EQ(result->method, Method::GET);
	EXPECT_EQ(result->path, "/search");
	EXPECT_EQ(result->uri, "q=test");
}

TEST(HttpParserTest, ParsesStatusLineCorrectly)
{
	auto parser = Parser();
	const auto response = std::string("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");

	const auto result = parser.Parse(AsBytes(response));
	ASSERT_TRUE(result);
	EXPECT_EQ(result->status.statusCode, StatusCode::OK);
}

TEST(HttpParserTest, EmptyLeadingLineDoesNotCrash)
{
	auto parser = Parser();
	const auto data = std::string("\r\n\r\n");

	EXPECT_NO_THROW(parser.Parse(AsBytes(data)));
}

TEST(ChunkBodyParserTest, ParsesMultipleChunksCorrectly)
{
	auto parser = ChunkBodyParser();
	const auto data = std::string("5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n");

	const auto result = parser.Parse(AsBytes(data));
	ASSERT_TRUE(result);
	EXPECT_EQ(AsString(*result), "hello world");
}

TEST(ChunkBodyParserTest, InvalidChunkSizeThrowsCleanException)
{
	auto parser = ChunkBodyParser();
	const auto data = std::string("ZZZ\r\n");

	EXPECT_THROW(parser.Parse(AsBytes(data)), ne::Exception);
}

TEST(HttpRequestTest, RejectsHeaderValueContainingCrlf)
{
	const auto injectedHeader = Header{ .name = "X-Test", .value = "value\r\nX-Injected: evil" };
	const auto attempt = [&injectedHeader] { Client::Get("http://example.com/").AddHeader(injectedHeader); };

	EXPECT_THROW(attempt(), ne::Exception);
}

TEST(HttpRequestTest, BuilderMethodsAreUsableAcrossMultipleStatements)
{
	auto request = Client::Get("http://example.com/");

	request.AddHeader({ .name = "X-Test-1", .value = "a" });
	request.AddHeader({ .name = "X-Test-2", .value = "b" });
	request.SetMode(Mode::Chunked);
	request.SetBody("payload");

	SUCCEED();
}

TEST(HttpServerResponseTest, RejectsHeaderValueContainingCrlf)
{
	auto response = Server::Response();
	const auto injectedHeader = Header{ .name = "X-Test", .value = "value\r\nX-Injected: evil" };
	const auto attempt = [&] { response.AddHeader(injectedHeader); };

	EXPECT_THROW(attempt(), ne::Exception);
}

TEST(HttpServerResponseTest, ChunkedResponseEndsWithProperTerminator)
{
	auto response = Server::Response();
	response.SetStatusCode(StatusCode::OK);
	response.SetBody(ne::string_view_t("hello"));

	const auto bytes = response.GetResponseString(Mode::Chunked);
	const auto text = AsString(bytes);

	EXPECT_NE(text.find("\r\n\r\n5\r\nhello\r\n0\r\n\r\n"), std::string::npos);
}

// Server::Route() previously returned Server&& (dangling rvalue ref); it now returns Server&.
// Verify that chained calls all resolve to the same object and that SetThreadPoolSize
// participates in the chain without crashing.
TEST(HttpServerTest, RouteReturnsSelfForChaining)
{
	using namespace std::chrono_literals;
	auto server = NebulaHttpServer("127.0.0.1", 0);

	auto& ref = server
		.SetThreadPoolSize(4)
		.SetTimeout(5000ms)
		.Route(Method::GET,  "/a", [](auto&&){})
		.Route(Method::POST, "/b", [](auto&&){});

	EXPECT_EQ(&ref, &server);
}
