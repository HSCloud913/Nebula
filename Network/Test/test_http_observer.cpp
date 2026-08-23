//
// Created by hscloud on 26. 8. 12.
//
// ServerObserver(액세스 로그 / 에러 / 연결 카운트) 훅 검증 — HTTP/1.1·HTTP/2 양쪽.
//
// 서버는 연결 하나의 실패가 다른 연결에 번지지 않도록 대부분의 에러를 삼킨다. 그 자체는 옳지만
// 운영자가 볼 수 있는 것이 사라지므로, 삼켜지는 정보가 훅으로는 반드시 나와야 한다.

#include <gtest/gtest.h>

#include <chrono>
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
using http::ServerBuilder;

namespace
{
#if defined(_WIN32)
	using TestEngine = IocpEngine;
#else
	using TestEngine = EpollEngine;
#endif

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

		if (auto bound = listener.Bind("127.0.0.1", 0); bound.IsError()) return R::Error(std::move(bound.Error()));
		if (auto listened = listener.Listen(); listened.IsError()) return R::Error(std::move(listened.Error()));

		const uint16_t port = listener.LocalPort();

		std::stop_source stopSource;
		auto serveTask = _builder->Serve(std::move(listener), _context, stopSource.get_token());

		return R::Ok(RunningServer{ std::move(stopSource), std::move(_builder), port, std::move(serveTask) });
	}

	void_t StopServer(Context& _context, RunningServer& _running)
	{
		_running.stopSource.request_stop();

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		while (!_running.serveTask.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 10 });
	}

	template <typename T>
	T DriveClient(Context& _context, ne::Task<T>& _primary, ne::Task<http::HttpResult<void_t>>& _server)
	{
		_primary.Resume();
		_server.Resume();

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!_primary.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 10 });

		if (!_primary.IsReady())
		{
			ADD_FAILURE() << "DriveClient: request did not complete within timeout";
			std::abort();
		}

		return _primary.await_resume();
	}

	std::string LoopbackUrl(const uint16_t _port, const std::string& _target) { return "http://127.0.0.1:" + std::to_string(_port) + _target; }
}

// ── HTTP/1.1: 요청 하나가 액세스 기록 한 건으로 관측된다 ──
TEST(HttpObserverTest, Http1EmitsAccessRecord)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	std::vector<http::AccessRecord> records;
	int_t openedCount = 0;
	int_t closedCount = 0;

	auto builder = std::make_unique<ServerBuilder>();
	builder->Get("/hello", [](const http::Request&) -> ne::Task<http::HttpResult<http::Response>> { co_return http::HttpResult<http::Response>::Ok(http::Response::Text(201, "observed")); });

	http::ServerObserver observer;
	observer.onAccess = [&records](const http::AccessRecord& _record) { records.push_back(_record); };
	observer.onConnection = [&openedCount, &closedCount](const bool_t _isOpened) { _isOpened ? ++openedCount : ++closedCount; };
	builder->Observe(std::move(observer));

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	auto requestTask = http::Get(LoopbackUrl(running.port, "/hello")).Send(context);
	auto result = DriveClient(context, requestTask, running.serveTask);
	ASSERT_TRUE(result.IsOk()) << result.Error().What();

	StopServer(context, running);

	ASSERT_EQ(records.size(), 1u) << "액세스 기록이 정확히 한 건이어야 한다";
	EXPECT_EQ(records[0].method, http::Method::GET);
	EXPECT_EQ(records[0].target, "/hello");
	EXPECT_EQ(records[0].statusCode, 201);
	EXPECT_EQ(records[0].version, http::Version::HTTP_1_1);
	EXPECT_EQ(records[0].responseBodyBytes, 8u); // "observed"

	EXPECT_EQ(openedCount, 1);
	EXPECT_EQ(closedCount, 1) << "연결 종료 통지가 누락되면 동시 연결 수를 계측할 수 없다";
}

// ── HTTP/1.1: 삼켜지는 요청 파싱 에러가 훅으로는 나온다 ──
//
// 서버는 형식 오류에 400 을 보내고 연결을 닫는데, 그 에러 자체는 Serve 의 반환값에 실리지 않는다
// (연결 단위 실패라 accept 루프를 끊지 않기 때문). 훅이 없으면 운영자가 원인을 알 수 없다.
TEST(HttpObserverTest, Http1ReportsSwallowedReadError)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	std::vector<std::string> phases;

	auto builder = std::make_unique<ServerBuilder>();
	builder->Get("/", [](const http::Request&) -> ne::Task<http::HttpResult<http::Response>> { co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, "ok")); });

	http::ServerObserver observer;
	observer.onError = [&phases](const http::HttpError&, const string_view_t _phase) { phases.emplace_back(_phase); };
	builder->Observe(std::move(observer));

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());
	running.serveTask.Resume();

	// 원시 소켓으로 콜론 없는 헤더 줄을 보내 MALFORMED_MESSAGE 를 유발한다.
	auto clientResult = Socket::Create(context, AF_INET);
	ASSERT_TRUE(clientResult.IsOk());
	Socket raw = std::move(clientResult.Value());

	auto connectTask = raw.Connect("127.0.0.1", running.port);
	connectTask.Resume();
	for (int_t i = 0; i < 20 && !connectTask.IsReady(); ++i) (void_t)context.RunOnce(std::chrono::milliseconds{ 5 });
	ASSERT_TRUE(connectTask.IsReady());
	ASSERT_TRUE(connectTask.await_resume().IsOk());

	constexpr string_view_t malformed = "GET / HTTP/1.1\r\nBrokenHeaderLine\r\n\r\n";
	auto sendTask = raw.Send(std::span<const byte_t>{ reinterpret_cast<const byte_t*>(malformed.data()), malformed.size() });
	sendTask.Resume();
	for (int_t i = 0; i < 20 && !sendTask.IsReady(); ++i) (void_t)context.RunOnce(std::chrono::milliseconds{ 5 });
	ASSERT_TRUE(sendTask.IsReady());
	ASSERT_TRUE(sendTask.await_resume().IsOk());

	for (int_t i = 0; i < 40; ++i) (void_t)context.RunOnce(std::chrono::milliseconds{ 5 });

	(void_t)raw.Close();
	StopServer(context, running);

	ASSERT_FALSE(phases.empty()) << "삼켜진 파싱 에러가 훅으로도 나오지 않았다";
	EXPECT_EQ(phases[0], "Read");
}

// ── HTTP/2: 같은 훅이 h2 경로에서도 동작하고 버전이 구분된다 ──
TEST(HttpObserverTest, Http2EmitsAccessRecordWithVersion)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	std::vector<http::AccessRecord> records;

	auto builder = std::make_unique<ServerBuilder>();
	builder->Version(http::Version::HTTP_2);
	builder->Post("/submit", [](const http::Request&) -> ne::Task<http::HttpResult<http::Response>> { co_return http::HttpResult<http::Response>::Ok(http::Response::Text(202, "queued")); });

	http::ServerObserver observer;
	observer.onAccess = [&records](const http::AccessRecord& _record) { records.push_back(_record); };
	builder->Observe(std::move(observer));

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	http::ClientSession session = http::Connect(Endpoint{ "127.0.0.1", running.port, false }, context, http::Version::HTTP_2);
	auto requestTask = session.Send(http::Post("/submit").Body(http::Body::FromString("payload")));
	auto result = DriveClient(context, requestTask, running.serveTask);
	ASSERT_TRUE(result.IsOk()) << result.Error().What();

	session.Close();
	StopServer(context, running);

	ASSERT_EQ(records.size(), 1u);
	EXPECT_EQ(records[0].method, http::Method::POST);
	EXPECT_EQ(records[0].target, "/submit");
	EXPECT_EQ(records[0].statusCode, 202);
	EXPECT_EQ(records[0].version, http::Version::HTTP_2) << "h1/h2 를 구분하지 못하면 액세스 로그의 가치가 절반이 된다";
	EXPECT_EQ(records[0].requestBodyBytes, 7u);  // "payload"
	EXPECT_EQ(records[0].responseBodyBytes, 6u); // "queued"
}

// ── 훅을 등록하지 않으면 아무 일도 일어나지 않는다(기본 비용 0) ──
TEST(HttpObserverTest, NoObserverIsHarmless)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto builder = std::make_unique<ServerBuilder>();
	builder->Get("/", [](const http::Request&) -> ne::Task<http::HttpResult<http::Response>> { co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, "bare")); });

	auto runningResult = StartServer(context, std::move(builder));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	auto requestTask = http::Get(LoopbackUrl(running.port, "/")).Send(context);
	auto result = DriveClient(context, requestTask, running.serveTask);
	ASSERT_TRUE(result.IsOk()) << result.Error().What();
	EXPECT_EQ(result.Value().statusCode, 200);

	StopServer(context, running);
}
