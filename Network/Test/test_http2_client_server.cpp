//
// Created by hscloud on 26. 7. 28.
//
// HTTP/2 클라이언트/서버 통합 테스트 — 통합 API(ne::network::http) 의 HTTP/2 경로를 검증한다.
//  1) 루프백(127.0.0.1, ephemeral port)으로 ServerBuilder 와 자유 함수 빌더/ClientSession(멀티플렉싱)을
//     h2c(평문, prior knowledge — 서버/클라이언트 모두 Version(HTTP_2) 고정) 경로로 검증.
//  2) 실제 h2 서버(nghttp2.org, https + ALPN "h2")에 SendSync() 로 요청·응답 왕복 검증(불가 시 SKIP).

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include "Base/Coroutine/Task.h"
#include "Io/Context.h"
#include "Io/Socket.h"
#include "Network/Protocol/Http/ClientBuilder.h"
#include "Network/Protocol/Http/ServerBuilder.h"

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
using http::Endpoint;
using http::ClientSession;
using http::ServerBuilder;

namespace
{
#if defined(_WIN32)
	using TestEngine = IocpEngine;
#else
	using TestEngine = EpollEngine;
#endif

	std::string BodyToString(const http::Body& _body)
	{
		std::string result;
		const auto view = _body.View();
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

	// 이미 kick 된 서버가 도는 동안 _primary 만 완료까지 구동한다.
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

	// 두 개의 요청 태스크를 동시에 완료까지 구동한다(멀티플렉싱 검증용).
	template <typename TPrimary>
	void_t DriveBoth(Context& _context, ne::Task<TPrimary>& _a, ne::Task<TPrimary>& _b, const std::chrono::milliseconds _timeout = std::chrono::seconds(10))
	{
		_a.Resume();
		_b.Resume();

		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while ((!_a.IsReady() || !_b.IsReady()) && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 10 });

		if (!_a.IsReady() || !_b.IsReady())
		{
			ADD_FAILURE() << "DriveBoth: requests did not complete within timeout";
			std::abort();
		}
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

	// ServerBuilder 는 힙에 고정해 serveTask 가 도는 동안 살려둔다(Serve 가 this 를 참조).
	struct RunningServer
	{
		std::stop_source stopSource;
		std::unique_ptr<ServerBuilder> builder;
		uint16_t port{};
		ne::Task<http::HttpResult<void_t>> serveTask;
	};

	IoResult<RunningServer> StartServer(Context& _context, std::unique_ptr<ServerBuilder> _builder)
	{
		using R = IoResult<RunningServer>;

		auto socketResult = Socket::Create(_context, AF_INET);
		if (socketResult.IsError()) return R::Error(std::move(socketResult.Error()));
		Socket listener = std::move(socketResult.Value());

		auto portResult = BindEphemeralListener(listener);
		if (portResult.IsError()) return R::Error(std::move(portResult.Error()));

		// 이 파일은 h2c(평문 HTTP/2, prior knowledge) 경로를 검증하므로 서버 버전을 HTTP_2 로 고정한다.
		_builder->Version(http::Version::HTTP_2);

		std::stop_source stopSource;
		auto serveTask = _builder->Serve(std::move(listener), _context, stopSource.get_token());

		return R::Ok(RunningServer{ std::move(stopSource), std::move(_builder), portResult.Value(), std::move(serveTask) });
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

	// 스트리밍 응답 라우트("/stream", "alpha:beta:gamma")를 가진 서버.
	std::unique_ptr<ServerBuilder> StreamingServer()
	{
		auto builder = std::make_unique<ServerBuilder>();
		builder->Get("/stream", [](const http::Request&) -> ne::Task<http::HttpResult<http::Response>>
					{
						http::Response response;
						response.statusCode = 200;
						response.body = StreamingBody({ "alpha:", "beta:", "gamma" });
						co_return http::HttpResult<http::Response>::Ok(std::move(response));
					});
		return builder;
	}

	// 어떤 경로/메서드든 target 을 200 본문으로 echo 하는 catch-all 서버(NotFound 로 등록).
	std::unique_ptr<ServerBuilder> EchoServer()
	{
		auto builder = std::make_unique<ServerBuilder>();
		builder->NotFound([](const http::Request& _request) -> ne::Task<http::HttpResult<http::Response>> { co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, std::string(_request.target))); });
		return builder;
	}
}



// ───────────────────────── 루프백(h2c): 기본 요청/응답 ─────────────────────────

TEST(Http2ClientServerTest, GetReturnsHandlerResponse)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto builder = std::make_unique<ServerBuilder>();
	builder->Get("/hello", [](const http::Request& _request) -> ne::Task<http::HttpResult<http::Response>>
				{
					EXPECT_EQ(_request.method, http::Method::GET);
					EXPECT_EQ(_request.target, "/hello");
					co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, "hello h2"));
				});

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	ClientSession session = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context, http::Version::HTTP_2);
	auto requestTask = session.Send(http::Get("/hello"));
	auto result = DriveClient(context, requestTask, running.serveTask);

	ASSERT_TRUE(result.IsOk()) << result.Error().What();
	EXPECT_EQ(result.Value().statusCode, 200);
	EXPECT_EQ(BodyToString(result.Value().body), "hello h2");

	session.Close();
	StopServer(context, running);
}

TEST(Http2ClientServerTest, PostBodyIsEchoedToHandler)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto builder = std::make_unique<ServerBuilder>();
	builder->Post("/echo", [](const http::Request& _request) -> ne::Task<http::HttpResult<http::Response>>
				{
					std::string body;
					const auto view = _request.body.View();
					for (const auto& segment : view.Segments()) body.append(reinterpret_cast<const char*>(segment.ptr), segment.length);
					co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, body));
				});

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	ClientSession session = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context, http::Version::HTTP_2);
	auto requestTask = session.Send(http::Post("/echo").Body("hello body over h2"));
	auto result = DriveClient(context, requestTask, running.serveTask);

	ASSERT_TRUE(result.IsOk()) << result.Error().What();
	EXPECT_EQ(result.Value().statusCode, 200);
	EXPECT_EQ(BodyToString(result.Value().body), "hello body over h2");

	session.Close();
	StopServer(context, running);
}

TEST(Http2ClientServerTest, CustomHeadersRoundTrip)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto builder = std::make_unique<ServerBuilder>();
	builder->Get("/headers", [](const http::Request& _request) -> ne::Task<http::HttpResult<http::Response>>
				{
					const auto custom = _request.headers.Get("x-custom");
					EXPECT_TRUE(custom.has_value());
					http::Response response = http::Response::Text(200, custom ? std::string(*custom) : std::string("<none>"));
					response.headers.Set("x-reply", "pong");
					co_return http::HttpResult<http::Response>::Ok(std::move(response));
				});

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	ClientSession session = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context, http::Version::HTTP_2);
	auto requestTask = session.Send(http::Get("/headers").Header("x-custom", "ping"));
	auto result = DriveClient(context, requestTask, running.serveTask);

	ASSERT_TRUE(result.IsOk()) << result.Error().What();
	EXPECT_EQ(result.Value().statusCode, 200);
	EXPECT_EQ(BodyToString(result.Value().body), "ping");
	const auto reply = result.Value().headers.Get("x-reply");
	ASSERT_TRUE(reply.has_value());
	EXPECT_EQ(*reply, "pong");

	session.Close();
	StopServer(context, running);
}

TEST(Http2ClientServerTest, RouteParamsAndQueryReachHandler)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto builder = std::make_unique<ServerBuilder>();
	builder->Get("/users/{id}", [](const http::Request& _request, const http::PathParams& _params) -> ne::Task<http::HttpResult<http::Response>>
				{
					const auto query = http::QueryParams::Parse(_request.target);
					const std::string body = std::string(_params.Get("id").value_or("<none>")) + "|" + std::string(query.Get("verbose").value_or("<none>"));
					co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, body));
				});

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	ClientSession session = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context, http::Version::HTTP_2);
	auto requestTask = session.Send(http::Get("/users/42?verbose=1"));
	auto result = DriveClient(context, requestTask, running.serveTask);

	ASSERT_TRUE(result.IsOk()) << result.Error().What();
	EXPECT_EQ(result.Value().statusCode, 200);
	EXPECT_EQ(BodyToString(result.Value().body), "42|1");

	session.Close();
	StopServer(context, running);
}

TEST(Http2ClientServerTest, NotFoundAndMethodNotAllowed)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto builder = std::make_unique<ServerBuilder>();
	builder->Get("/only-get", [](const http::Request&) -> ne::Task<http::HttpResult<http::Response>> { co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, "ok")); });

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());
	running.serveTask.Resume();

	ClientSession session = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context, http::Version::HTTP_2);

	auto missing = session.Send(http::Get("/nope"));
	auto missingResult = DrivePrimary(context, missing);
	ASSERT_TRUE(missingResult.IsOk()) << missingResult.Error().What();
	EXPECT_EQ(missingResult.Value().statusCode, 404);

	auto wrongMethod = session.Send(http::Post("/only-get"));
	auto wrongResult = DrivePrimary(context, wrongMethod);
	ASSERT_TRUE(wrongResult.IsOk()) << wrongResult.Error().What();
	EXPECT_EQ(wrongResult.Value().statusCode, 405);
	EXPECT_TRUE(wrongResult.Value().headers.Get("Allow").has_value());

	session.Close();
	StopServer(context, running);
}



TEST(Http2ClientServerTest, OneShotAsyncSend)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto runningResult = StartServer(context, EchoServer());
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	// 자유 함수 빌더의 1회성 비동기 Send(ctx) — 연결을 열어 요청 1건 보내고 내부에서 정상 종료(DrainClose).
	auto requestTask = http::Get(LoopbackUrl(running.port, "/oneshot")).Version(http::Version::HTTP_2).Send(context);
	auto result = DriveClient(context, requestTask, running.serveTask);

	ASSERT_TRUE(result.IsOk()) << result.Error().What();
	EXPECT_EQ(result.Value().statusCode, 200);
	EXPECT_EQ(BodyToString(result.Value().body), "/oneshot");

	StopServer(context, running);
}



// ───────────────────────── 루프백(h2c): 동시 연결(Serve 동시 처리) ─────────────────────────

TEST(Http2ClientServerTest, ServeHandlesConcurrentConnections)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto runningResult = StartServer(context, EchoServer());
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());
	running.serveTask.Resume();

	// 세션 A(연결 1)가 열려 있는 동안 세션 B(연결 2)의 요청이 처리되어야 한다(연결을 순차 accept 하면 B 는 영원히 대기).
	ClientSession a = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context, http::Version::HTTP_2);
	ClientSession b = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context, http::Version::HTTP_2);

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

	a.Close();
	b.Close();
	StopServer(context, running);
}



// ───────────────────────── 루프백(h2c): teardown(리퍼) ─────────────────────────

TEST(Http2ClientServerTest, DroppedSessionIsReapedByLoop)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto runningResult = StartServer(context, EchoServer());
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	{
		ClientSession dropped = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context, http::Version::HTTP_2);
		auto task = dropped.Send(http::Get("/first"));
		auto result = DriveClient(context, task, running.serveTask);
		ASSERT_TRUE(result.IsOk()) << result.Error().What();
		// Shutdown()/Close() 없이 스코프를 벗어난다 — 소멸자가 정리를 리퍼(이벤트 루프)에 위임해야 한다.
	}

	// 리퍼가 정리를 마칠 시간을 준다(in-flight 취소 완료 → 드라이버 종료 → connection 파괴).
	for (int_t i = 0; i < 20; ++i) (void_t)context.RunOnce(std::chrono::milliseconds{ 5 });

	// 루프/서버가 여전히 건강해야 하고, 테스트 종료 시 엔진 파괴에서 크래시가 없어야 한다.
	ClientSession fresh = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context, http::Version::HTTP_2);
	auto task2 = fresh.Send(http::Get("/second"));
	auto result2 = DrivePrimary(context, task2);
	ASSERT_TRUE(result2.IsOk()) << result2.Error().What();
	EXPECT_EQ(BodyToString(result2.Value().body), "/second");

	fresh.Close();
	StopServer(context, running);
}



// ───────────────────────── 루프백(h2c): 스트리밍 ─────────────────────────

TEST(Http2ClientServerTest, ServerStreamsResponseBody)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto runningResult = StartServer(context, StreamingServer());
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	ClientSession session = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context, http::Version::HTTP_2);

	auto first = session.Send(http::Get("/stream"));
	auto firstResult = DriveClient(context, first, running.serveTask);
	ASSERT_TRUE(firstResult.IsOk()) << firstResult.Error().What();
	EXPECT_EQ(firstResult.Value().statusCode, 200);
	EXPECT_EQ(BodyToString(firstResult.Value().body), "alpha:beta:gamma");

	// END_STREAM 종결이 정확해야 같은 연결의 다음 스트림이 성공한다.
	auto second = session.Send(http::Get("/stream"));
	auto secondResult = DrivePrimary(context, second);
	ASSERT_TRUE(secondResult.IsOk()) << secondResult.Error().What();
	EXPECT_EQ(BodyToString(secondResult.Value().body), "alpha:beta:gamma");

	session.Close();
	StopServer(context, running);
}

TEST(Http2ClientServerTest, ClientStreamDeliversHeadAndBody)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto runningResult = StartServer(context, StreamingServer());
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	int_t seenStatus = 0;
	std::string collected;
	http::ResponseCallbacks sink;
	sink.onHead = [&](const int_t _statusCode, const string_view_t, const http::Headers&) { seenStatus = _statusCode; return true; };
	sink.onBody = [&](const std::span<const byte_t> _chunk) { collected.append(reinterpret_cast<const char*>(_chunk.data()), _chunk.size()); return true; };

	auto task = http::Get(LoopbackUrl(running.port, "/stream")).Version(http::Version::HTTP_2).Stream(sink, context);
	auto result = DriveClient(context, task, running.serveTask);

	ASSERT_TRUE(result.IsOk()) << result.Error().What();
	EXPECT_EQ(seenStatus, 200);
	EXPECT_EQ(collected, "alpha:beta:gamma");

	StopServer(context, running);
}



// ───────────────────────── 루프백(h2c): keep-alive & 멀티플렉싱 ─────────────────────────

TEST(Http2ClientServerTest, ClientSessionReusesForMultipleRequests)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto runningResult = StartServer(context, EchoServer());
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());
	running.serveTask.Resume();

	ClientSession session = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context, http::Version::HTTP_2);

	auto first = session.Send(http::Get("/first"));
	auto firstResult = DrivePrimary(context, first);
	ASSERT_TRUE(firstResult.IsOk()) << firstResult.Error().What();
	EXPECT_EQ(BodyToString(firstResult.Value().body), "/first");
	EXPECT_TRUE(session.IsOpen());

	auto second = session.Send(http::Get("/second"));
	auto secondResult = DrivePrimary(context, second);
	ASSERT_TRUE(secondResult.IsOk()) << secondResult.Error().What();
	EXPECT_EQ(BodyToString(secondResult.Value().body), "/second");

	session.Close();
	StopServer(context, running);
}

TEST(Http2ClientServerTest, ConcurrentStreamsOnOneConnection)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto runningResult = StartServer(context, EchoServer());
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());
	running.serveTask.Resume();

	ClientSession session = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context, http::Version::HTTP_2);

	// 먼저 한 번 보내 연결을 수립한다(이후 두 요청은 같은 연결에서 동시에 진행).
	auto warmup = session.Send(http::Get("/warmup"));
	auto warmupResult = DrivePrimary(context, warmup);
	ASSERT_TRUE(warmupResult.IsOk()) << warmupResult.Error().What();

	auto a = session.Send(http::Get("/alpha"));
	auto b = session.Send(http::Get("/beta"));
	DriveBoth(context, a, b);

	auto ra = a.await_resume();
	auto rb = b.await_resume();
	ASSERT_TRUE(ra.IsOk()) << ra.Error().What();
	ASSERT_TRUE(rb.IsOk()) << rb.Error().What();
	EXPECT_EQ(BodyToString(ra.Value().body), "/alpha");
	EXPECT_EQ(BodyToString(rb.Value().body), "/beta");

	session.Close();
	StopServer(context, running);
}



// ───────────────────────── 실제 h2 서버(https + ALPN) ─────────────────────────

TEST(Http2RealServerTest, NghttpGetOverTls)
{
	auto result = http::Get("https://nghttp2.org/").Header("user-agent", "nebula-h2-test").Version(http::Version::HTTP_2).SendSync();

	if (result.IsError())
	{
		GTEST_SKIP() << "real h2 server unreachable: " << result.Error().What();
	}

	// nghttp2.org 루트는 보통 200. 서버 사정에 따라 3xx 도 허용.
	EXPECT_GE(result.Value().statusCode, 200);
	EXPECT_LT(result.Value().statusCode, 400);
}

// AUTO(기본) — https 는 ALPN 에 {"h2","http/1.1"} 을 제안하고 협상 결과를 따른다. nghttp2.org 는 h2 를
// 고르므로 사실상 AUTO→h2 경로 검증이며, 협상이 어떻게 되든 요청은 성공해야 한다.
TEST(Http2RealServerTest, AutoNegotiatesOverTls)
{
	auto result = http::Get("https://nghttp2.org/").Header("user-agent", "nebula-h2-test").SendSync();

	if (result.IsError())
	{
		GTEST_SKIP() << "real h2 server unreachable: " << result.Error().What();
	}

	EXPECT_GE(result.Value().statusCode, 200);
	EXPECT_LT(result.Value().statusCode, 400);
}
