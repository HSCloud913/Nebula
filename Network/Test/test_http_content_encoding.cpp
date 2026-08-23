//
// Created by hscloud on 26. 8. 23.
//
// 클라이언트의 콘텐츠 인코딩 자동 처리 검증 — Accept-Encoding 광고와 Content-Encoding 자동 해제.
//
// 여기서 확인하려는 계약은 두 가지다. (1) 우리가 광고했으면 압축 응답은 호출자에게 평문으로 보인다.
// (2) 호출자가 직접 Accept-Encoding 을 넣었으면 우리는 손대지 않는다 — 그 경우 압축 바이트 자체가
// 목적일 수 있기 때문이다(프록시·캐시 저장·재전송).

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "Base/Coroutine/Task.h"
#include "Io/Context.h"
#include "Io/Socket.h"
#include "Compress/Gzip.h"
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

namespace
{
#if defined(_WIN32)
	using TestEngine = IocpEngine;
#else
	using TestEngine = EpollEngine;
#endif

	// "Hello, DEFLATE world!" 을 실제 gzip(1) 로 압축한 바이트 — 서버가 이걸 그대로 내보낸다.
	constexpr unsigned char GzipBody[] = {
		0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x03, 0xf3, 0x48,
		0xcd, 0xc9, 0xc9, 0xd7, 0x51, 0x70, 0x71, 0x75, 0xf3, 0x71, 0x0c, 0x71,
		0x55, 0x28, 0xcf, 0x2f, 0xca, 0x49, 0x51, 0x04, 0x00, 0x2f, 0x1b, 0xaa,
		0xa2, 0x15, 0x00, 0x00, 0x00
	};

	constexpr string_view_t PlainText = "Hello, DEFLATE world!";

	struct RunningServer
	{
		std::stop_source stopSource;
		std::unique_ptr<http::ServerBuilder> builder;
		uint16_t port{};
		ne::Task<http::HttpResult<void_t>> serveTask;
	};

	IoResult<RunningServer> StartServer(Context& _context, std::unique_ptr<http::ServerBuilder> _builder)
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

	// 이미 kick 된 서버가 도는 동안 _primary 만 완료까지 구동한다(같은 연결로 여러 요청을 순차로 보낼 때).
	template <typename T>
	T DrivePrimary(Context& _context, ne::Task<T>& _primary)
	{
		_primary.Resume();

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!_primary.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 10 });

		if (!_primary.IsReady())
		{
			ADD_FAILURE() << "DrivePrimary: request did not complete within timeout";
			std::abort();
		}

		return _primary.await_resume();
	}

	std::string LoopbackUrl(const uint16_t _port, const std::string& _target) { return "http://127.0.0.1:" + std::to_string(_port) + _target; }

	// gzip 본문을 Content-Encoding: gzip 으로 내보내고, 받은 Accept-Encoding 을 _seen 에 적어 두는 서버.
	std::unique_ptr<http::ServerBuilder> MakeGzipServer(string_t& _seen)
	{
		auto builder = std::make_unique<http::ServerBuilder>();
		builder->Get("/gz", [&_seen](const http::Request& _request) -> ne::Task<http::HttpResult<http::Response>> {
			if (const auto accept = _request.headers.Get("Accept-Encoding")) _seen = string_t{ *accept };

			http::Response response;
			response.statusCode = 200;
			response.headers.Set("Content-Type", "text/plain");
			response.headers.Set("Content-Encoding", "gzip");
			response.body = http::Body(std::vector<byte_t>(reinterpret_cast<const byte_t*>(std::begin(GzipBody)), reinterpret_cast<const byte_t*>(std::end(GzipBody))));

			co_return http::HttpResult<http::Response>::Ok(std::move(response));
		});

		return builder;
	}

	[[nodiscard]] std::string AsString(const http::Body& _body)
	{
		std::string text;
		// View() 가 값을 돌려주므로 체인을 지역 변수로 붙든다 — 범위식에 직접 쓰면 임시가 먼저 죽는다.
		const ne::memory::BufferChain chain = _body.View();
		for (const auto& segment : chain.Segments()) text.append(reinterpret_cast<const char*>(segment.ptr), segment.length);

		return text;
	}
}



TEST(HttpContentEncodingTest, AdvertisesAcceptEncodingAndDecompressesTransparently)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	string_t seenAcceptEncoding;
	auto runningResult = StartServer(context, MakeGzipServer(seenAcceptEncoding));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	auto requestTask = http::Get(LoopbackUrl(running.port, "/gz")).Send(context);
	auto result = DriveClient(context, requestTask, running.serveTask);
	ASSERT_TRUE(result.IsOk()) << result.Error().What();

	StopServer(context, running);

	EXPECT_NE(seenAcceptEncoding.find("gzip"), string_t::npos) << "해제 가능한 인코딩을 광고하지 않으면 서버가 압축해 보낼 이유가 없다";

	EXPECT_EQ(AsString(result.Value().body), PlainText) << "호출자에게는 평문으로 보여야 한다";

	// 본문은 평문이 됐으므로 헤더도 그렇게 말해야 한다 — 남겨 두면 이 응답을 재전송하는 코드가 깨진다.
	EXPECT_FALSE(result.Value().headers.Has("Content-Encoding"));
	if (const auto length = result.Value().headers.Get("Content-Length")) EXPECT_EQ(*length, std::to_string(PlainText.size()));
}

TEST(HttpContentEncodingTest, LeavesBodyAloneWhenCallerSetsAcceptEncoding)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	string_t seenAcceptEncoding;
	auto runningResult = StartServer(context, MakeGzipServer(seenAcceptEncoding));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	auto requestTask = http::Get(LoopbackUrl(running.port, "/gz")).Header("Accept-Encoding", "gzip").Send(context);
	auto result = DriveClient(context, requestTask, running.serveTask);
	ASSERT_TRUE(result.IsOk()) << result.Error().What();

	StopServer(context, running);

	EXPECT_EQ(seenAcceptEncoding, "gzip") << "호출자가 지정한 헤더를 덮어쓰면 안 된다";

	// 압축된 채로 와야 한다 — 손대지 않았다는 증거는 헤더가 남아 있고 본문 크기가 압축 크기라는 것이다.
	EXPECT_TRUE(result.Value().headers.Has("Content-Encoding"));
	EXPECT_EQ(result.Value().body.Size(), sizeof(GzipBody));
}

TEST(HttpContentEncodingTest, DecompressesOverKeepAliveSession)
{
	// 세션(연결 재사용) 경로는 원샷 경로와 다른 코드가 요청을 조립한다 — 둘의 동작이 갈리지 않는지 본다.
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	string_t seenAcceptEncoding;
	auto runningResult = StartServer(context, MakeGzipServer(seenAcceptEncoding));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	running.serveTask.Resume(); // 서버를 한 번만 kick — 이후 RunOnce 가 구동한다(요청마다 다시 Resume 하면 안 된다).

	auto session = http::Connect(http::Endpoint{ "127.0.0.1", running.port, false }, context);

	for (int_t attempt = 0; attempt < 2; ++attempt)
	{
		auto requestTask = session.Send(http::Get("/gz"));
		auto result = DrivePrimary(context, requestTask);
		ASSERT_TRUE(result.IsOk()) << "attempt " << attempt << ": " << result.Error().What();

		EXPECT_EQ(AsString(result.Value().body), PlainText) << "attempt " << attempt;
		EXPECT_FALSE(result.Value().headers.Has("Content-Encoding")) << "attempt " << attempt;
	}

	session.Close();
	StopServer(context, running);
}



// ───────────────────────── 서버 응답 압축 ─────────────────────────

namespace
{
	constexpr string_view_t LargeText =
		"이 응답은 압축 하한(1KB)을 넘겨야 하므로 충분히 길어야 합니다. "
		"압축이 실제로 적용되는지를 보려면 반복이 있는 텍스트가 좋습니다 — "
		"반복이 없으면 압축해도 줄지 않아 서버가 압축을 건너뛰고, 그러면 이 테스트는 "
		"압축 경로를 지나지 않은 채로 통과해 버립니다. ";

	[[nodiscard]] string_t RepeatedBody(const int_t _times)
	{
		string_t text;
		for (int_t index = 0; index < _times; ++index) text.append(LargeText);

		return text;
	}

	// 큰 텍스트 응답과, 이미 압축된 것처럼 표시된 응답을 함께 제공하는 서버.
	std::unique_ptr<http::ServerBuilder> MakeCompressingServer(const bool_t _isCompressionEnabled)
	{
		auto builder = std::make_unique<http::ServerBuilder>();

		builder->Get("/big", [](const http::Request&) -> ne::Task<http::HttpResult<http::Response>> {
			co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, RepeatedBody(20)));
		});

		builder->Get("/tiny", [](const http::Request&) -> ne::Task<http::HttpResult<http::Response>> {
			co_return http::HttpResult<http::Response>::Ok(http::Response::Text(200, "small"));
		});

		builder->Get("/binary", [](const http::Request&) -> ne::Task<http::HttpResult<http::Response>> {
			http::Response response;
			response.statusCode = 200;
			response.headers.Set("Content-Type", "image/png"); // 허용 목록에 없다 — 이미 압축된 형식
			response.body = http::Body(std::vector<byte_t>(4096, byte_t{ 0x41 }));

			co_return http::HttpResult<http::Response>::Ok(std::move(response));
		});

		if (_isCompressionEnabled) builder->Compress();

		return builder;
	}
}

TEST(HttpServerCompressionTest, CompressesLargeTextResponse)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto runningResult = StartServer(context, MakeCompressingServer(true));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	// 클라이언트가 Accept-Encoding 을 자동으로 붙이고 자동으로 푼다 — 호출자에게는 평문으로 보여야 한다.
	auto requestTask = http::Get(LoopbackUrl(running.port, "/big")).Send(context);
	auto result = DriveClient(context, requestTask, running.serveTask);
	ASSERT_TRUE(result.IsOk()) << result.Error().What();

	StopServer(context, running);

	EXPECT_EQ(AsString(result.Value().body), RepeatedBody(20));

	// 서버가 실제로 압축했다면 클라이언트가 풀었으므로 Content-Encoding 은 지워져 있다. 압축이
	// 일어났다는 증거는 Vary 헤더다 — 이것은 클라이언트가 지우지 않는다.
	const auto vary = result.Value().headers.Get("Vary");
	ASSERT_TRUE(vary.has_value()) << "Vary: Accept-Encoding 이 없으면 중간 캐시가 잘못된 응답을 돌려줄 수 있다";
	EXPECT_NE(vary->find("Accept-Encoding"), string_view_t::npos);
}

TEST(HttpServerCompressionTest, ActuallySendsFewerBytesOnTheWire)
{
	// 위 테스트는 "압축 경로를 지났다" 만 본다. 실제로 줄었는지는 클라이언트가 풀기 전의 바이트를
	// 봐야 알 수 있으므로, Accept-Encoding 을 직접 지정해 자동 해제를 끈다.
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto runningResult = StartServer(context, MakeCompressingServer(true));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	auto requestTask = http::Get(LoopbackUrl(running.port, "/big")).Header("Accept-Encoding", "gzip").Send(context);
	auto result = DriveClient(context, requestTask, running.serveTask);
	ASSERT_TRUE(result.IsOk()) << result.Error().What();

	StopServer(context, running);

	const string_t expected = RepeatedBody(20);

	ASSERT_EQ(result.Value().headers.Get("Content-Encoding"), "gzip");
	EXPECT_LT(result.Value().body.Size(), expected.size() / 2) << "압축했다면서 절반도 줄지 않았다";

	// 받은 바이트가 정말 gzip 이고 원본과 같은지 직접 확인한다.
	const std::vector<byte_t> received = [&result] {
		std::vector<byte_t> bytes;
		const ne::memory::BufferChain chain = result.Value().body.View();
		for (const auto& segment : chain.Segments()) bytes.insert(bytes.end(), segment.ptr, segment.ptr + segment.length);

		return bytes;
	}();

	const auto decoded = ne::compress::GzipDecompress(received);
	ASSERT_TRUE(decoded.IsOk()) << decoded.Error().What();
	EXPECT_EQ(string_t(reinterpret_cast<const char*>(decoded.Value().data()), decoded.Value().size()), expected);
}

TEST(HttpServerCompressionTest, DoesNothingWhenDisabled)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto runningResult = StartServer(context, MakeCompressingServer(false));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());

	auto requestTask = http::Get(LoopbackUrl(running.port, "/big")).Header("Accept-Encoding", "gzip").Send(context);
	auto result = DriveClient(context, requestTask, running.serveTask);
	ASSERT_TRUE(result.IsOk()) << result.Error().What();

	StopServer(context, running);

	// 기본이 꺼짐이어야 한다 — 켜지 않았는데 압축되면 CPU 를 조용히 쓰는 것이다.
	EXPECT_FALSE(result.Value().headers.Has("Content-Encoding"));
	EXPECT_FALSE(result.Value().headers.Has("Vary"));
	EXPECT_EQ(result.Value().body.Size(), RepeatedBody(20).size());
}

TEST(HttpServerCompressionTest, SkipsSmallAndIncompressibleResponses)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto runningResult = StartServer(context, MakeCompressingServer(true));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());
	running.serveTask.Resume();

	{
		// 하한(1KB) 미만 — 압축해도 이득이 없고 오히려 커질 수 있다.
		auto task = http::Get(LoopbackUrl(running.port, "/tiny")).Header("Accept-Encoding", "gzip").Send(context);
		auto result = DrivePrimary(context, task);
		ASSERT_TRUE(result.IsOk()) << result.Error().What();

		EXPECT_FALSE(result.Value().headers.Has("Content-Encoding")) << "작은 응답까지 압축하면 CPU 만 쓴다";
	}
	{
		// image/png — 허용 목록에 없다. 이미 압축된 형식을 다시 압축하는 것은 순손실이다.
		auto task = http::Get(LoopbackUrl(running.port, "/binary")).Header("Accept-Encoding", "gzip").Send(context);
		auto result = DrivePrimary(context, task);
		ASSERT_TRUE(result.IsOk()) << result.Error().What();

		EXPECT_FALSE(result.Value().headers.Has("Content-Encoding")) << "허용 목록에 없는 Content-Type 을 압축했다";
	}

	StopServer(context, running);
}

TEST(HttpServerCompressionTest, DoesNotCompressWhenClientCannotDecode)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto runningResult = StartServer(context, MakeCompressingServer(true));
	ASSERT_TRUE(runningResult.IsOk()) << runningResult.Error().What();
	auto running = std::move(runningResult.Value());
	running.serveTask.Resume();

	{
		// 우리가 만들 수 없는 것만 광고한 클라이언트 — 압축하면 클라이언트가 풀 수 없다.
		auto task = http::Get(LoopbackUrl(running.port, "/big")).Header("Accept-Encoding", "br").Send(context);
		auto result = DrivePrimary(context, task);
		ASSERT_TRUE(result.IsOk()) << result.Error().What();

		EXPECT_FALSE(result.Value().headers.Has("Content-Encoding"));
	}
	{
		// q=0 은 명시적 거부다. 이를 무시하면 클라이언트가 풀 수 없는 응답을 보내게 된다.
		auto task = http::Get(LoopbackUrl(running.port, "/big")).Header("Accept-Encoding", "gzip;q=0").Send(context);
		auto result = DrivePrimary(context, task);
		ASSERT_TRUE(result.IsOk()) << result.Error().What();

		EXPECT_FALSE(result.Value().headers.Has("Content-Encoding")) << "gzip;q=0 은 gzip 을 보내지 말라는 뜻이다";
	}

	StopServer(context, running);
}
