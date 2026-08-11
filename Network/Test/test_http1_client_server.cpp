//
// Created by hscloud on 26. 7. 21.
//
// HTTP/1.1 클라이언트/서버 통합 테스트 — 통합 API(ne::network::http) 의 HTTP/1.1 경로를 검증한다.
//  1) 루프백(127.0.0.1, ephemeral port)으로 ServerBuilder 와 ClientBuilder(자유 함수)/ClientSession(keep-alive)/
//     스트리밍/타임아웃/헤더인젝션 검증(평문 AUTO → HTTP/1.1 경로).
//  2) 실제 외부 서버(example.com http·https, postman-echo.com)에 자유 함수 빌더 + SendSync() 로 요청·응답 왕복 검증(불가 시 SKIP).
//     https 는 Version(HTTP_1_1) 로 고정해 ALPN 협상 없이 HTTP/1.1 경로를 검증한다.

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>
#include "Base/Coroutine/Task.h"
#include "Io/Context.h"
#include "Io/Socket.h"
#include "Time/Timer/TimerWheel.h"
#include "Network/Protocol/Http/ResponseCallbacks.h"
#include "Network/Protocol/Http/ClientBuilder.h"
#include "Network/Protocol/Http/ServerBuilder.h"
#include "Network/Protocol/Http/Internal/Http1/Parser.h"

#if defined(_WIN32)
#include "Base/WinsockApi.h"
#   include "Io/Internal/Engine/Iocp/IocpEngine.h"
#elif defined(IS_POSIX)
#   include <arpa/inet.h>
#   include "Io/Internal/Engine/Epoll/EpollEngine.h"
#endif

using namespace ne;
using namespace ne::io;
namespace http = ne::network::http;
namespace http_1 = ne::network::http_1; // internal(BuildRequestHead) 단위 테스트에만 사용
using http::Endpoint;
using http::ResponseCallbacks;
using http::ClientSession;
using http::ServerBuilder;

namespace
{
#if defined(_WIN32)
	using TestEngine = IocpEngine;
#else
	using TestEngine = EpollEngine;
#endif

	// body(소유/뷰 무관)를 std::string 으로 모은다.
	std::string BodyToString(const http::Body& _body)
	{
		std::string result;
		const auto view = _body.View(); // 명명 지역변수 — 임시 BufferChain 이 순회 전에 파괴되는 dangling 방지
		for (const auto& segment : view.Segments()) result.append(reinterpret_cast<const char*>(segment.ptr), segment.length);
		return result;
	}

	// _primary 완료까지 _primary 와 (아직 kick 안 된) _server 를 함께 구동한다.
	template <typename TPrimary>
	TPrimary DriveClient(Context& _context, ne::Task<TPrimary>& _primary, ne::Task<http::HttpResult<void_t>>& _server, const std::chrono::milliseconds _timeout = std::chrono::seconds(10))
	{
		_primary.Resume();
		_server.Resume();

		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while (!_primary.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 10 });

		if (!_primary.IsReady())
		{
			ADD_FAILURE() << "DriveClient: request did not complete within timeout";
			std::abort();
		}

		return _primary.await_resume();
	}

	// 이미 kick 된 서버가 도는 동안 _primary 만 완료까지 구동한다(같은 연결로 여러 요청을 순차로 보낼 때).
	template <typename TPrimary>
	TPrimary DrivePrimary(Context& _context, ne::Task<TPrimary>& _primary, const std::chrono::milliseconds _timeout = std::chrono::seconds(10))
	{
		_primary.Resume();

		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while (!_primary.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 10 });

		if (!_primary.IsReady())
		{
			ADD_FAILURE() << "DrivePrimary: task did not complete within timeout";
			std::abort();
		}

		return _primary.await_resume();
	}

	// 단독 태스크(서버 없이)를 완료까지 구동한다.
	template <typename T>
	T Drive(Context& _context, ne::Task<T>& _task, const std::chrono::milliseconds _timeout = std::chrono::seconds(5))
	{
		_task.Resume();
		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while (!_task.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 10 });

		if (!_task.IsReady())
		{
			ADD_FAILURE() << "Drive: task did not complete within timeout";
			std::abort();
		}

		return _task.await_resume();
	}

	IoResult<uint16_t> BindEphemeralListener(Socket& _listener)
	{
		using R = IoResult<uint16_t>;
		if (auto r = _listener.Bind("127.0.0.1", 0); r.IsError()) return R::Error(std::move(r.Error()));
		if (auto r = _listener.Listen(); r.IsError()) return R::Error(std::move(r.Error()));

		sockaddr_in addr{};
		socklen_t addrLen = sizeof(addr);
		if (::getsockname(_listener.Handle(), reinterpret_cast<sockaddr*>(&addr), &addrLen) != 0) return R::Error(IoError{ IoErrorKind::OS_FAILURE, "getsockname failed" });

		return R::Ok(::ntohs(addr.sin_port));
	}

	// ServerBuilder 는 힙에 고정해 serveTask 가 도는 동안 살려둔다(Serve 의 첫 resume 에서 라우트를 읽음).
	// 리스너 Socket 은 값으로 Serve 에 넘겨져 그 코루틴 프레임이 소유하므로 별도 고정이 필요 없다.
	struct RunningServer
	{
		std::stop_source stopSource;
		std::unique_ptr<ServerBuilder> builder;
		uint16_t port{};
		ne::Task<http::HttpResult<void_t>> serveTask;
	};

	IoResult<RunningServer> StartServer(Context& _context, ServerBuilder _builder)
	{
		using R = IoResult<RunningServer>;

		auto socketResult = Socket::Create(_context, AF_INET);
		if (socketResult.IsError()) return R::Error(std::move(socketResult.Error()));
		Socket listener = std::move(socketResult.Value());

		auto portResult = BindEphemeralListener(listener);
		if (portResult.IsError()) return R::Error(std::move(portResult.Error()));

		auto builder = std::make_unique<ServerBuilder>(std::move(_builder));
		std::stop_source stopSource;
		auto serveTask = builder->Serve(std::move(listener), _context, stopSource.get_token());

		return R::Ok(RunningServer{ std::move(stopSource), std::move(builder), portResult.Value(), std::move(serveTask) });
	}

	void_t StopServer(Context& _context, RunningServer& _running)
	{
		_running.stopSource.request_stop();

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		while (!_running.serveTask.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 10 });
	}

	std::string LoopbackUrl(const uint16_t _port, const std::string& _target) { return "http://127.0.0.1:" + std::to_string(_port) + _target; }

	// 스트리밍 본문 생산자용 헬퍼 — 상태(진행 인덱스)는 shared_ptr 로 소유하고, 코루틴은 자유 함수로 둔다
	// (캡처 있는 코루틴 람다의 수명 함정 회피).
	ne::Task<http::HttpResult<std::vector<byte_t>>> NextChunk(std::shared_ptr<std::size_t> _index, std::shared_ptr<std::vector<std::string>> _chunks)
	{
		using R = http::HttpResult<std::vector<byte_t>>;
		if (*_index >= _chunks->size()) co_return R::Ok(std::vector<byte_t>{}); // 빈 조각 = EOF
		const std::string& chunk = (*_chunks)[(*_index)++];
		co_return R::Ok(std::vector<byte_t>(chunk.begin(), chunk.end()));
	}

	http::Body StreamingBody(std::vector<std::string> _chunks)
	{
		auto chunks = std::make_shared<std::vector<std::string>>(std::move(_chunks));
		auto index = std::make_shared<std::size_t>(0);
		return http::Body::FromProducer([chunks, index] { return NextChunk(index, chunks); });
	}

	// 어떤 경로/메서드든 target 을 200 본문으로 echo 하는 catch-all 서버(NotFound 로 등록).
	ServerBuilder EchoServer()
	{
		ServerBuilder builder;
		builder.NotFound([](const http::Request& _request) -> ne::Task<http::HttpResult<http::Response>> { co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, std::string(_request.target))); });
		return builder;
	}
}



// ───────────────────────── 루프백: 기본 클라이언트/서버 ─────────────────────────

TEST(Http1ClientServerTest, GetReturnsHandlerResponse)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	ServerBuilder builder;
	builder.Get("/hello", [](const http::Request& _request) -> ne::Task<http::HttpResult<http::Response>>
				{
					EXPECT_EQ(_request.method, http::Method::GET);
					EXPECT_EQ(_request.target, "/hello");
					co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, "hello world"));
				});

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	auto requestTask = http::Get(LoopbackUrl(running.port, "/hello")).Send(context);
	auto result = DriveClient(context, requestTask, running.serveTask);

	ASSERT_TRUE(result.IsOk()) << result.Error().What();
	EXPECT_EQ(result.Value().statusCode, 200);
	EXPECT_EQ(result.Value().reason, "OK");
	EXPECT_EQ(BodyToString(result.Value().body), "hello world");

	StopServer(context, running);
}

TEST(Http1ClientServerTest, PostBodyIsDeliveredToHandler)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	ServerBuilder builder;
	builder.Post("/echo", [](const http::Request& _request) -> ne::Task<http::HttpResult<http::Response>>
				{
					EXPECT_EQ(_request.method, http::Method::POST);
					co_return http::HttpResult<http::Response>::Ok(http::Response::Json(201, BodyToString(_request.body)));
				});

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	auto requestTask = http::Post(LoopbackUrl(running.port, "/echo")).Body(R"({"n":42})").Send(context);
	auto result = DriveClient(context, requestTask, running.serveTask);

	ASSERT_TRUE(result.IsOk()) << result.Error().What();
	EXPECT_EQ(result.Value().statusCode, 201);
	EXPECT_EQ(result.Value().headers.Get("Content-Type"), "application/json");
	EXPECT_EQ(BodyToString(result.Value().body), R"({"n":42})");

	StopServer(context, running);
}

TEST(Http1ClientServerTest, ConnectionRefusedReturnsTransportError)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto requestTask = http::Get("http://127.0.0.1:1/").Send(context);
	auto result = Drive(context, requestTask);

	EXPECT_TRUE(result.IsError());
}



// ───────────────────────── 루프백: keep-alive(ClientSession · 외부 Context) ─────────────────────────

TEST(Http1ClientServerTest, ClientSessionReusesForMultipleRequests)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto runningResult = StartServer(context, EchoServer());
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());
	running.serveTask.Resume(); // 서버를 한 번만 kick — 이후 RunOnce 가 구동한다.

	ClientSession session = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context);

	auto first = session.Send(http::Get("/first"));
	auto firstResult = DrivePrimary(context, first);
	ASSERT_TRUE(firstResult.IsOk()) << firstResult.Error().What();
	EXPECT_EQ(firstResult.Value().statusCode, 200);
	EXPECT_EQ(BodyToString(firstResult.Value().body), "/first");
	EXPECT_TRUE(session.IsOpen()); // keep-alive — 재사용 가능 상태로 열려 있어야 함

	auto second = session.Send(http::Get("/second"));
	auto secondResult = DrivePrimary(context, second);
	ASSERT_TRUE(secondResult.IsOk()) << secondResult.Error().What();
	EXPECT_EQ(secondResult.Value().statusCode, 200);
	EXPECT_EQ(BodyToString(secondResult.Value().body), "/second");

	StopServer(context, running);
}



// ───────────────────────── 루프백: 동시 연결(Serve 동시 처리) ─────────────────────────

TEST(Http1ClientServerTest, ServeHandlesConcurrentConnections)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto runningResult = StartServer(context, EchoServer());
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());
	running.serveTask.Resume();

	// A 가 keep-alive 로 열려 있는 동안 B 의 요청이 처리되어야 한다(연결을 순차 accept 하면 B 는 A 가 닫힐 때까지 영원히 대기).
	ClientSession a = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context);
	ClientSession b = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context);

	auto first = a.Send(http::Get("/a1"));
	auto firstResult = DrivePrimary(context, first);
	ASSERT_TRUE(firstResult.IsOk()) << firstResult.Error().What();
	EXPECT_EQ(BodyToString(firstResult.Value().body), "/a1");
	EXPECT_TRUE(a.IsOpen()); // A 는 여전히 서버 쪽 연결을 물고 있다

	auto second = b.Send(http::Get("/b1"));
	auto secondResult = DrivePrimary(context, second);
	ASSERT_TRUE(secondResult.IsOk()) << secondResult.Error().What();
	EXPECT_EQ(BodyToString(secondResult.Value().body), "/b1");

	// 두 연결이 모두 열린 상태에서 A 로 되돌아가도 응답해야 한다(연결별 독립 태스크).
	auto third = a.Send(http::Get("/a2"));
	auto thirdResult = DrivePrimary(context, third);
	ASSERT_TRUE(thirdResult.IsOk()) << thirdResult.Error().What();
	EXPECT_EQ(BodyToString(thirdResult.Value().body), "/a2");

	// StopServer 는 열린 연결(A/B)이 남은 채 stop 을 요청한다 — Serve 의 취소·drain 경로도 함께 검증된다.
	StopServer(context, running);
}



// ───────────────────────── 루프백: 새 연결 여러 개(Serve 재접속) ─────────────────────────

TEST(Http1ClientServerTest, ServeHandlesSequentialConnections)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto runningResult = StartServer(context, EchoServer());
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());
	running.serveTask.Resume();

	// 매 요청이 새 연결(요청 기본 Connection: close) — 서버가 연결마다 다시 Accept 해야 한다.
	for (const std::string target : { std::string("/one"), std::string("/two"), std::string("/three") })
	{
		auto task = http::Get(LoopbackUrl(running.port, target)).Send(context);
		auto result = DrivePrimary(context, task);
		ASSERT_TRUE(result.IsOk()) << result.Error().What();
		EXPECT_EQ(result.Value().statusCode, 200);
		EXPECT_EQ(BodyToString(result.Value().body), target);
	}

	StopServer(context, running);
}



// ───────────────────────── 루프백: ServerBuilder 라우팅 ─────────────────────────

TEST(Http1ClientServerTest, ServerBuilderRoutesAndHandles404And405)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	ServerBuilder builder;
	builder.Get("/health", [](const http::Request&) -> ne::Task<http::HttpResult<http::Response>> { co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, "ok")); })
		   .Post("/users", [](const http::Request&) -> ne::Task<http::HttpResult<http::Response>> { co_return http::HttpResult<http::Response>::Ok(http::Response::Status(201)); });

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());
	running.serveTask.Resume();

	// 1) GET /health → 200 "ok"
	{
		auto task = http::Get(LoopbackUrl(running.port, "/health")).Send(context);
		auto result = DrivePrimary(context, task);
		ASSERT_TRUE(result.IsOk()) << result.Error().What();
		EXPECT_EQ(result.Value().statusCode, 200);
		EXPECT_EQ(BodyToString(result.Value().body), "ok");
	}
	// 2) GET /missing → 404
	{
		auto task = http::Get(LoopbackUrl(running.port, "/missing")).Send(context);
		auto result = DrivePrimary(context, task);
		ASSERT_TRUE(result.IsOk()) << result.Error().What();
		EXPECT_EQ(result.Value().statusCode, 404);
	}
	// 3) POST /health(등록된 경로, 잘못된 메서드) → 405 + Allow: GET
	{
		auto task = http::Post(LoopbackUrl(running.port, "/health")).Send(context);
		auto result = DrivePrimary(context, task);
		ASSERT_TRUE(result.IsOk()) << result.Error().What();
		EXPECT_EQ(result.Value().statusCode, 405);
		EXPECT_EQ(result.Value().headers.Get("Allow"), "GET");
	}

	StopServer(context, running);
}



// ───────────────────────── 루프백: 경로 파라미터 + 쿼리 ─────────────────────────

TEST(Http1ClientServerTest, RouteParamsAndQueryReachHandler)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	ServerBuilder builder;
	builder.Get("/users/{id}", [](const http::Request& _request, const http::PathParams& _params) -> ne::Task<http::HttpResult<http::Response>>
				{
					const auto query = http::QueryParams::Parse(_request.target);
					const std::string body = std::string(_params.Get("id").value_or("<none>")) + "|" + std::string(query.Get("verbose").value_or("<none>"));
					co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, body));
				});

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	auto requestTask = http::Get(LoopbackUrl(running.port, "/users/42?verbose=1")).Send(context);
	auto result = DriveClient(context, requestTask, running.serveTask);

	ASSERT_TRUE(result.IsOk()) << result.Error().What();
	EXPECT_EQ(result.Value().statusCode, 200);
	EXPECT_EQ(BodyToString(result.Value().body), "42|1");

	StopServer(context, running);
}



// ───────────────────────── 루프백: 서버 스트리밍 응답(chunked) ─────────────────────────

TEST(Http1ClientServerTest, ServerStreamsChunkedResponse)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	ServerBuilder builder;
	builder.Get("/stream", [](const http::Request&) -> ne::Task<http::HttpResult<http::Response>>
				{
					http::Response response;
					response.statusCode = 200;
					response.body = StreamingBody({ "alpha:", "beta:", "gamma" });
					co_return http::HttpResult<http::Response>::Ok(std::move(response));
				});

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());
	running.serveTask.Resume();

	ClientSession session = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context);

	auto first = session.Send(http::Get("/stream"));
	auto firstResult = DrivePrimary(context, first);
	ASSERT_TRUE(firstResult.IsOk()) << firstResult.Error().What();
	EXPECT_EQ(firstResult.Value().statusCode, 200);
	EXPECT_EQ(firstResult.Value().headers.Get("Transfer-Encoding"), "chunked");
	EXPECT_EQ(BodyToString(firstResult.Value().body), "alpha:beta:gamma");

	// chunked 종결이 정확해야 같은 연결의 다음 요청이 성공한다(keep-alive 프레이밍 검증).
	auto second = session.Send(http::Get("/stream"));
	auto secondResult = DrivePrimary(context, second);
	ASSERT_TRUE(secondResult.IsOk()) << secondResult.Error().What();
	EXPECT_EQ(BodyToString(secondResult.Value().body), "alpha:beta:gamma");

	StopServer(context, running);
}



// ───────────────────────── 루프백: 스트리밍 응답 ─────────────────────────

TEST(Http1ClientServerTest, StreamingDeliversHeadAndBody)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	const std::string payload(500, 'x');
	ServerBuilder builder;
	builder.Get("/big", [payload](const http::Request&) -> ne::Task<http::HttpResult<http::Response>> { co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, payload)); });

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	int_t seenStatus = 0;
	std::string collected;
	ResponseCallbacks sink;
	sink.onHead = [&](const int_t _statusCode, const string_view_t, const http::Headers&) { seenStatus = _statusCode; return true; };
	sink.onBody = [&](const std::span<const byte_t> _chunk) { collected.append(reinterpret_cast<const char*>(_chunk.data()), _chunk.size()); return true; };

	auto task = http::Get(LoopbackUrl(running.port, "/big")).Stream(sink, context);
	auto result = DriveClient(context, task, running.serveTask);

	ASSERT_TRUE(result.IsOk()) << result.Error().What();
	EXPECT_EQ(seenStatus, 200);
	EXPECT_EQ(collected, payload);

	StopServer(context, running);
}



// ───────────────────────── 루프백: 빌더 헤더/본문 조립 ─────────────────────────

TEST(Http1ClientServerTest, ClientBuilderSendsHeaderAndBody)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	ServerBuilder builder;
	builder.Post("/build", [](const http::Request& _request) -> ne::Task<http::HttpResult<http::Response>>
				{
					const auto trace = _request.headers.Get("X-Trace-Id");
					const std::string echo = std::string(trace.value_or("")) + ":" + BodyToString(_request.body);
					co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, echo));
				});

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	auto task = http::Post(LoopbackUrl(running.port, "/build"))
					.Header("X-Trace-Id", "xyz")
					.Body("payload")
					.Send(context);
	auto result = DriveClient(context, task, running.serveTask);

	ASSERT_TRUE(result.IsOk()) << result.Error().What();
	EXPECT_EQ(result.Value().statusCode, 200);
	EXPECT_EQ(BodyToString(result.Value().body), "xyz:payload");

	StopServer(context, running);
}



// ───────────────────────── 루프백: 리다이렉트 자동 추적(FollowRedirects) ─────────────────────────

TEST(Http1ClientServerTest, FollowRedirectsChasesLocation)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	ServerBuilder builder;
	builder.Get("/old", [](const http::Request&) -> ne::Task<http::HttpResult<http::Response>>
				{
					auto response = http::Response::Status(302);
					response.headers.Set("Location", "/new");
					co_return http::HttpResult<http::Response>::Ok(std::move(response));
				})
		   .Get("/new", [](const http::Request&) -> ne::Task<http::HttpResult<http::Response>> { co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, "moved here")); });

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	// FollowRedirects() 를 켜면 Location 을 따라가 최종 200 을 받는다.
	auto followTask = http::Get(LoopbackUrl(running.port, "/old")).FollowRedirects().Send(context);
	auto followed = DriveClient(context, followTask, running.serveTask);

	ASSERT_TRUE(followed.IsOk()) << followed.Error().What();
	EXPECT_EQ(followed.Value().statusCode, 200);
	EXPECT_EQ(BodyToString(followed.Value().body), "moved here");

	// 기본값(0)은 추적하지 않고 3xx 응답을 그대로 돌려준다.
	auto rawTask = http::Get(LoopbackUrl(running.port, "/old")).Send(context);
	auto raw = DrivePrimary(context, rawTask);

	ASSERT_TRUE(raw.IsOk()) << raw.Error().What();
	EXPECT_EQ(raw.Value().statusCode, 302);
	EXPECT_EQ(raw.Value().headers.Get("Location").value_or(""), "/new");

	StopServer(context, running);
}



// ───────────────────────── 루프백: BasicAuth 헤더 조립 ─────────────────────────

TEST(Http1ClientServerTest, BasicAuthSetsAuthorizationHeader)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	ServerBuilder builder;
	builder.Get("/secret", [](const http::Request& _request) -> ne::Task<http::HttpResult<http::Response>>
				{
					const auto authorization = _request.headers.Get("Authorization");
					co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, std::string(authorization.value_or("(none)"))));
				});

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	auto task = http::Get(LoopbackUrl(running.port, "/secret")).BasicAuth("user", "pass").Send(context);
	auto result = DriveClient(context, task, running.serveTask);

	ASSERT_TRUE(result.IsOk()) << result.Error().What();
	EXPECT_EQ(BodyToString(result.Value().body), "Basic dXNlcjpwYXNz"); // base64("user:pass")

	StopServer(context, running);
}



// ───────────────────────── 루프백: per-request 타임아웃 ─────────────────────────

TEST(Http1ClientServerTest, RequestTimeoutReturnsTimeoutError)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	ne::time::TimerWheel wheel;
	Context context{ engine, &wheel }; // Timeout/SleepFor 는 타이머 휠 전제

	// 핸들러가 클라이언트 타임아웃보다 오래 지연 → 클라이언트는 시한 초과로 끝나야 한다.
	ServerBuilder builder;
	builder.Get("/slow", [&context](const http::Request&) -> ne::Task<http::HttpResult<http::Response>>
				{
					co_await context.SleepFor(std::chrono::milliseconds(800));
					co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, "late"));
				});

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());
	running.serveTask.Resume();

	auto task = http::Get(LoopbackUrl(running.port, "/slow")).Timeout(std::chrono::milliseconds(150)).Send(context);
	auto result = DrivePrimary(context, task);

	ASSERT_TRUE(result.IsError());
	EXPECT_EQ(result.Error().Kind(), http::HttpErrorKind::TIMEOUT);

	StopServer(context, running);
}



// ───────────────────────── 헤더 CR/LF 인젝션 차단(순수 단위) ─────────────────────────

TEST(Http1ClientServerTest, HeaderInjectionIsRejectedBeforeSend)
{
	http::Request bad{ .method = http::Method::GET, .target = "/" };
	bad.headers.Add("X-Evil", "value\r\nInjected: 1");
	auto builtHeader = http_1::internal::BuildRequestHead(bad);
	ASSERT_TRUE(builtHeader.IsError());
	EXPECT_EQ(builtHeader.Error().Kind(), http::HttpErrorKind::MALFORMED_MESSAGE);

	http::Request badTarget{ .method = http::Method::GET, .target = "/path\r\nX: y" };
	EXPECT_TRUE(http_1::internal::BuildRequestHead(badTarget).IsError());
}



// ───────────────────────── 실제 외부 서버(네트워크) ─────────────────────────
// 네트워크가 불가하면 실패 대신 SKIP 한다. 실제 요청/응답 왕복을 검증한다.

TEST(Http1RealServerTest, HttpGetExampleDotCom)
{
	auto result = http::Get("http://example.com/").Timeout(std::chrono::seconds(15)).SendSync();
	if (result.IsError()) GTEST_SKIP() << "network unavailable: " << result.Error().What();

	EXPECT_EQ(result.Value().statusCode, 200);
	EXPECT_FALSE(BodyToString(result.Value().body).empty());
	EXPECT_NE(BodyToString(result.Value().body).find("Example Domain"), std::string::npos);
}

TEST(Http1RealServerTest, HttpsGetExampleDotCom)
{
	auto result = http::Get("https://example.com/").Version(http::Version::HTTP_1_1).Timeout(std::chrono::seconds(15)).SendSync();
	if (result.IsError()) GTEST_SKIP() << "network/TLS unavailable: " << result.Error().What();

	EXPECT_EQ(result.Value().statusCode, 200);
	EXPECT_NE(BodyToString(result.Value().body).find("Example Domain"), std::string::npos);
}

TEST(Http1RealServerTest, HttpsGetStatusEndpoint)
{
	auto result = http::Get("https://postman-echo.com/status/200").Version(http::Version::HTTP_1_1).Timeout(std::chrono::seconds(20)).SendSync();
	if (result.IsError()) GTEST_SKIP() << "network/TLS unavailable: " << result.Error().What();
	// 외부 서비스는 rate-limit/일시 장애(5xx/429)가 날 수 있다 — 응답을 정상 수신·파싱한 것만으로
	// 클라이언트는 검증된 것이므로 그 경우는 실패가 아니라 SKIP 으로 처리한다.
	if (result.Value().statusCode >= 500 || result.Value().statusCode == 429)
		GTEST_SKIP() << "echo service unavailable: HTTP " << result.Value().statusCode;

	EXPECT_EQ(result.Value().statusCode, 200);
}

TEST(Http1RealServerTest, HttpsPostEchoesBody)
{
	auto result = http::Post("https://postman-echo.com/post")
					.Header("Content-Type", "application/json")
					.Body(R"({"hello":"nebula"})")
					.Version(http::Version::HTTP_1_1)
					.Timeout(std::chrono::seconds(20))
					.SendSync();
	if (result.IsError()) GTEST_SKIP() << "network/TLS unavailable: " << result.Error().What();
	if (result.Value().statusCode >= 500 || result.Value().statusCode == 429)
		GTEST_SKIP() << "echo service unavailable: HTTP " << result.Value().statusCode;

	EXPECT_EQ(result.Value().statusCode, 200);
	// 에코 서버는 받은 본문을 JSON 의 "data"/"json" 필드로 되돌려준다.
	EXPECT_NE(BodyToString(result.Value().body).find("nebula"), std::string::npos);
}

// 블로킹 keep-alive 세션(자체 Runtime · 외부 Context 불필요)으로 같은 서버에 2회 요청.
TEST(Http1RealServerTest, BlockingSessionSendsMultipleRequests)
{
	auto session = http::ConnectSync("https://example.com", ne::io::EngineType::PROACTOR, http::Version::HTTP_1_1); // 블로킹 세션(Context 불필요)
	ASSERT_TRUE(session.has_value());

	auto first = session->Send(http::Get("/"));
	if (first.IsError()) GTEST_SKIP() << "network/TLS unavailable: " << first.Error().What();
	EXPECT_EQ(first.Value().statusCode, 200);

	auto second = session->Send(http::Get("/"));
	if (second.IsError()) GTEST_SKIP() << "network unavailable: " << second.Error().What();
	EXPECT_EQ(second.Value().statusCode, 200);
}
