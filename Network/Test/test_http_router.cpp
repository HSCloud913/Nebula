//
// Created by hscloud on 26. 7. 29.
//
// http 공용층 라우팅/파라미터 단위 테스트.
//  1) UrlDecode/QueryParams — 퍼센트 디코딩, '+' 공백 변환, 다중 값, 값 없는 파라미터.
//  2) internal::Router — 리터럴/{name}/{*name} 매칭, 등록 순서 우선, 405(Allow)/404/NotFound.
//     Dispatch 는 동기 핸들러와 함께면 Resume() 한 번에 완료되므로 엔진 없이 검증한다.

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include "Base/Coroutine/Task.h"
#include "Network/Protocol/Http/Message/Params.h"
#include "Network/Protocol/Http/Internal/Router.h"

using namespace ne;
namespace http = ne::network::http;
using http::PathParams;
using http::QueryParams;
using http::internal::Router;

namespace
{
	std::string BodyToString(const http::Body& _body)
	{
		std::string result;
		const auto view = _body.View();
		for (const auto& segment : view.Segments()) result.append(reinterpret_cast<const char*>(segment.ptr), segment.length);
		return result;
	}

	// 동기 핸들러만 등록된 라우터의 Dispatch 를 완료까지 구동해 결과를 꺼낸다.
	http::HttpResult<http::Response> Dispatch(const Router& _router, const http::Method _method, const std::string& _target)
	{
		http::Request request;
		request.method = _method;
		request.target = _target;

		auto task = _router.Dispatch(request);
		task.Resume();
		EXPECT_TRUE(task.IsReady()) << "Dispatch did not complete synchronously for " << _target;
		return task.await_resume();
	}

	// 매칭된 경로 파라미터들을 "name=value;..." 로 직렬화해 200 본문으로 돌려주는 핸들러.
	Router::RouteHandler EchoParams(std::initializer_list<const char*> _names)
	{
		return [names = std::vector<std::string>(_names.begin(), _names.end())](const http::Request&, const PathParams& _params) -> ne::Task<http::HttpResult<http::Response>>
		{
			std::string body;
			for (const auto& name : names)
			{
				body += name;
				body += '=';
				body += std::string(_params.Get(name).value_or("<missing>"));
				body += ';';
			}
			co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, body));
		};
	}
}



// ───────────────────────── UrlDecode ─────────────────────────

TEST(UrlDecodeTest, DecodesPercentSequences)
{
	EXPECT_EQ(http::UrlDecode("a%20b"), "a b");
	EXPECT_EQ(http::UrlDecode("%2Fetc%2Fpasswd"), "/etc/passwd");
	EXPECT_EQ(http::UrlDecode("%ec%95%88"), "\xec\x95\x88"); // 소문자 hex 도 허용
}

TEST(UrlDecodeTest, PlusHandling)
{
	EXPECT_EQ(http::UrlDecode("a+b"), "a+b");        // 경로 모드 — '+' 유지
	EXPECT_EQ(http::UrlDecode("a+b", true), "a b");  // 쿼리 모드 — '+' 는 공백
}

TEST(UrlDecodeTest, InvalidSequencesArePreserved)
{
	EXPECT_EQ(http::UrlDecode("%zz"), "%zz");   // 잘못된 hex
	EXPECT_EQ(http::UrlDecode("100%"), "100%"); // 잘린 시퀀스
	EXPECT_EQ(http::UrlDecode("%4"), "%4");
}



// ───────────────────────── QueryParams ─────────────────────────

TEST(QueryParamsTest, ParsesNameValuePairs)
{
	const auto query = QueryParams::Parse("/search?q=hello+world&page=2");
	EXPECT_EQ(query.Count(), 2u);
	EXPECT_EQ(query.Get("q"), "hello world");
	EXPECT_EQ(query.Get("page"), "2");
	EXPECT_FALSE(query.Get("missing").has_value());
}

TEST(QueryParamsTest, NoQueryStringYieldsEmpty)
{
	EXPECT_TRUE(QueryParams::Parse("/plain/path").IsEmpty());
	EXPECT_TRUE(QueryParams::Parse("/trailing?").IsEmpty());
}

TEST(QueryParamsTest, RepeatedNamesAndFlags)
{
	const auto query = QueryParams::Parse("/t?tag=a&tag=b&debug");
	EXPECT_EQ(query.Get("tag"), "a"); // 첫 매치
	const auto all = query.GetAll("tag");
	ASSERT_EQ(all.size(), 2u);
	EXPECT_EQ(all[0], "a");
	EXPECT_EQ(all[1], "b");
	EXPECT_TRUE(query.Has("debug")); // 값 없는 파라미터
	EXPECT_EQ(query.Get("debug"), "");
}

TEST(QueryParamsTest, DecodesEncodedNameAndValue)
{
	const auto query = QueryParams::Parse("/t?a%20b=c%26d");
	EXPECT_EQ(query.Get("a b"), "c&d");
}



// ───────────────────────── Router: 매칭 ─────────────────────────

TEST(HttpRouterTest, LiteralPathStillMatchesExactly)
{
	Router router;
	router.Add(http::Method::GET, "/health", EchoParams({}));

	EXPECT_EQ(Dispatch(router, http::Method::GET, "/health").Value().statusCode, 200);
	EXPECT_EQ(Dispatch(router, http::Method::GET, "/health?probe=1").Value().statusCode, 200); // 쿼리 제외
	EXPECT_EQ(Dispatch(router, http::Method::GET, "/health/x").Value().statusCode, 404);
	EXPECT_EQ(Dispatch(router, http::Method::GET, "/healt").Value().statusCode, 404);
}

TEST(HttpRouterTest, SingleParamCapturesSegment)
{
	Router router;
	router.Add(http::Method::GET, "/users/{id}", EchoParams({ "id" }));

	const auto result = Dispatch(router, http::Method::GET, "/users/42");
	EXPECT_EQ(BodyToString(result.Value().body), "id=42;");

	EXPECT_EQ(Dispatch(router, http::Method::GET, "/users").Value().statusCode, 404);          // 세그먼트 부족
	EXPECT_EQ(Dispatch(router, http::Method::GET, "/users/42/posts").Value().statusCode, 404); // 세그먼트 초과
	EXPECT_EQ(Dispatch(router, http::Method::GET, "/users//x").Value().statusCode, 404);       // 빈 세그먼트 거부
}

TEST(HttpRouterTest, MultipleParamsAndPercentDecoding)
{
	Router router;
	router.Add(http::Method::GET, "/users/{uid}/posts/{pid}", EchoParams({ "uid", "pid" }));

	const auto result = Dispatch(router, http::Method::GET, "/users/kim%20a/posts/7?draft=1");
	EXPECT_EQ(BodyToString(result.Value().body), "uid=kim a;pid=7;");
}

TEST(HttpRouterTest, CatchAllCapturesRemainder)
{
	Router router;
	router.Add(http::Method::GET, "/files/{*path}", EchoParams({ "path" }));

	EXPECT_EQ(BodyToString(Dispatch(router, http::Method::GET, "/files/a/b/c.txt").Value().body), "path=a/b/c.txt;");
	EXPECT_EQ(BodyToString(Dispatch(router, http::Method::GET, "/files").Value().body), "path=;"); // 빈 나머지 허용
	EXPECT_EQ(Dispatch(router, http::Method::GET, "/other/a").Value().statusCode, 404);
}

TEST(HttpRouterTest, RegistrationOrderDecidesPrecedence)
{
	Router router;
	router.Add(http::Method::GET, "/users/me", EchoParams({}));         // 리터럴을 먼저 등록
	router.Add(http::Method::GET, "/users/{id}", EchoParams({ "id" }));

	EXPECT_EQ(BodyToString(Dispatch(router, http::Method::GET, "/users/me").Value().body), "");        // 리터럴 승리
	EXPECT_EQ(BodyToString(Dispatch(router, http::Method::GET, "/users/77").Value().body), "id=77;");
}



// ───────────────────────── Router: 405 / 404 / NotFound ─────────────────────────

TEST(HttpRouterTest, MethodMismatchReturns405WithAllow)
{
	Router router;
	router.Add(http::Method::GET, "/items/{id}", EchoParams({ "id" }));
	router.Add(http::Method::DELETE_, "/items/{id}", EchoParams({ "id" }));

	const auto result = Dispatch(router, http::Method::POST, "/items/5");
	EXPECT_EQ(result.Value().statusCode, 405);
	EXPECT_EQ(result.Value().headers.Get("Allow"), "GET, DELETE");
}

TEST(HttpRouterTest, NotFoundHandlerIsUsedWhenNothingMatches)
{
	Router router;
	router.Add(http::Method::GET, "/known", EchoParams({}));
	router.SetNotFound([](const http::Request& _request) -> ne::Task<http::HttpResult<http::Response>>
					   { co_return http::HttpResult<http::Response>::Ok(http::Response::Text(404, "custom:" + std::string(_request.target))); });

	const auto result = Dispatch(router, http::Method::GET, "/unknown");
	EXPECT_EQ(result.Value().statusCode, 404);
	EXPECT_EQ(BodyToString(result.Value().body), "custom:/unknown");
}
