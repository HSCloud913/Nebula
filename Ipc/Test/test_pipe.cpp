//
// Created by nebula on 24. 11. 3.
//

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "../../Ipc/Pipe.h"

#if defined(IS_POSIX)
#   include "Engine/Epoll/EpollEngine.h"
    using TestIoEngine = ne::io::EpollEngine;
#elif defined(_WIN32)
#   include "Engine/Iocp/IocpEngine.h"
    using TestIoEngine = ne::io::IocpEngine;
#endif

using ne::ipc::Pipe;

namespace
{
	std::span<const std::byte> AsBytes(const std::string& _string)
	{
		return std::span(reinterpret_cast<const std::byte*>(_string.data()), _string.size());
	}

	std::string AsString(const std::span<const std::byte> _bytes)
	{
		return std::string(reinterpret_cast<const ne::char_t*>(_bytes.data()), _bytes.size());
	}

	// 최대 5 초 동안 IIoEngine::RunOnce() 를 구동해 _flag 가 true 가 되길 대기.
	// POSIX(epoll/io_uring)·Windows(IOCP) 모두 완료 통지는 RunOnce() 호출 스레드에서만
	// 온다 — 두 플랫폼 다 이 방식으로 이벤트 루프를 돌려야 코루틴이 재개된다.
	void DriveEngine(TestIoEngine& _engine, const std::atomic<bool>& _flag,
	                  const std::chrono::milliseconds _timeout = std::chrono::seconds(5))
	{
		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while (!_flag.load(std::memory_order_acquire) &&
		       std::chrono::steady_clock::now() < deadline)
			(void)_engine.RunOnce(10);
	}
}

// ─── 기존 동기 테스트 ────────────────────────────────────────────────────────

TEST(PipeTest, ListenConnectReadWrite)
{
	auto server = Pipe("nebula-pipe-test-readwrite");
	auto serverThread = std::thread([&server] { server.Listen(); });

	auto client = Pipe("nebula-pipe-test-readwrite");
	client.Connect();
	serverThread.join();

	const auto toServer = std::string("nebula-ipc-pipe-to-server");
	ASSERT_TRUE(client.Write(AsBytes(toServer)));

	auto serverBuffer = std::vector<std::byte>(64);
	const auto serverReceived = server.Read(serverBuffer);
	ASSERT_GE(serverReceived, 0);
	serverBuffer.resize(serverReceived);
	EXPECT_EQ(AsString(serverBuffer), toServer);

	const auto toClient = std::string("nebula-ipc-pipe-to-client");
	ASSERT_TRUE(server.Write(AsBytes(toClient)));

	auto clientBuffer = std::vector<std::byte>(64);
	const auto clientReceived = client.Read(clientBuffer);
	ASSERT_GE(clientReceived, 0);
	clientBuffer.resize(clientReceived);
	EXPECT_EQ(AsString(clientBuffer), toClient);
}

// ─── 비동기 테스트 ────────────────────────────────────────────────────────────

// ReadAsync 가 IIoEngine 기반으로 정상 수신하는지 검증.
//   - 서버: ReadAsync 로 대기 (코루틴 suspend)
//   - 클라이언트 스레드: Connect() 후 동기 Write
//   - 이벤트 루프 구동: RunOnce() 를 돌려야 완료가 코루틴으로 전달됨 (POSIX/Windows 공통)
TEST(PipeTest, ReadAsyncBasic)
{
#if defined(IS_POSIX) || defined(_WIN32)
	TestIoEngine engine;

	auto server = Pipe("nebula-pipe-async-basic");
	auto serverListenThread = std::thread([&server] { server.Listen(); });

	auto client = Pipe("nebula-pipe-async-basic");
	client.Connect();
	serverListenThread.join();

	const auto message = std::string("nebula-async-receive");

	std::atomic<bool> done{ false };
	ne::Result<std::size_t, ne::OsError> received = ne::Result<std::size_t, ne::OsError>::Ok(0);
	std::vector<std::byte> buffer(64);

	auto task = [&]() -> ne::Task<void>
	{
		received = co_await server.ReadAsync(buffer, engine);
		done.store(true, std::memory_order_release);
	}();
	task.Resume();

	// 클라이언트에서 메시지 전송
	std::thread([&client, &message]
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		(void)client.Write(AsBytes(message));
	}).detach();

	DriveEngine(engine, done);

	ASSERT_TRUE(done.load()) << "ReadAsync timed out";
	ASSERT_TRUE(received.IsOk()) << received.Error().What();
	buffer.resize(received.Value());
	EXPECT_EQ(AsString(buffer), message);
#else
	GTEST_SKIP() << "Platform not supported";
#endif
}

// ReadAsync 대기 중 이벤트 루프가 다른 작업을 처리할 수 있음을 검증.
//   - 코루틴 A: ReadAsync 로 suspend
//   - 코루틴 B: 즉시 완료하는 카운터 증가
//   - A 가 suspend 된 사이에 B 가 실행됐는지 확인 (비동기성 증명)
TEST(PipeTest, ReadAsyncNonBlocking)
{
#if defined(IS_POSIX) || defined(_WIN32)
	TestIoEngine engine;

	auto server = Pipe("nebula-pipe-async-nonblock");
	auto serverListenThread = std::thread([&server] { server.Listen(); });

	auto client = Pipe("nebula-pipe-async-nonblock");
	client.Connect();
	serverListenThread.join();

	std::atomic<bool> aDone{ false };
	std::atomic<bool> bDone{ false };
	ne::Result<std::size_t, ne::OsError> aResult = ne::Result<std::size_t, ne::OsError>::Ok(0);
	std::vector<std::byte> buffer(64);

	// A: 메시지 도착까지 suspend
	auto taskA = [&]() -> ne::Task<void>
	{
		aResult = co_await server.ReadAsync(buffer, engine);
		aDone.store(true, std::memory_order_release);
	}();
	taskA.Resume(); // I/O 제출 후 즉시 반환 (코루틴 suspend)

	// B: suspend 없이 즉시 완료
	auto taskB = [&]() -> ne::Task<void>
	{
		bDone.store(true, std::memory_order_release);
		co_return;
	}();
	taskB.Resume();

	// B 는 A 가 suspend 된 사이에 실행돼야 함
	EXPECT_TRUE(bDone.load(std::memory_order_acquire))
		<< "B must run while A is suspended";

	// 클라이언트에서 메시지 전송
	std::thread([&client]
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		(void)client.Write(AsBytes(std::string("ping")));
	}).detach();

	DriveEngine(engine, aDone);

	ASSERT_TRUE(aDone.load()) << "ReadAsync timed out";
	ASSERT_TRUE(aResult.IsOk()) << aResult.Error().What();
	buffer.resize(aResult.Value());
	EXPECT_EQ(AsString(buffer), "ping");
#else
	GTEST_SKIP() << "Platform not supported";
#endif
}

// WriteAsync 가 데이터를 전송하고 상대방이 동기 Read 로 수신하는지 검증
TEST(PipeTest, WriteAsyncBasic)
{
#if defined(IS_POSIX) || defined(_WIN32)
	TestIoEngine engine;

	auto server = Pipe("nebula-pipe-async-write");
	auto serverListenThread = std::thread([&server] { server.Listen(); });

	auto client = Pipe("nebula-pipe-async-write");
	client.Connect();
	serverListenThread.join();

	const auto message = std::string("nebula-async-send");

	std::atomic<bool> done{ false };
	ne::Result<std::size_t, ne::OsError> writeResult = ne::Result<std::size_t, ne::OsError>::Ok(0);

	auto task = [&]() -> ne::Task<void>
	{
		writeResult = co_await client.WriteAsync(AsBytes(message), engine);
		done.store(true, std::memory_order_release);
	}();
	task.Resume();

	DriveEngine(engine, done);

	ASSERT_TRUE(done.load()) << "WriteAsync timed out";
	ASSERT_TRUE(writeResult.IsOk()) << writeResult.Error().What();
	EXPECT_EQ(writeResult.Value(), message.size());

	auto buffer = std::vector<std::byte>(64);
	const auto received = server.Read(buffer);
	ASSERT_GE(received, 0);
	buffer.resize(received);
	EXPECT_EQ(AsString(buffer), message);
#else
	GTEST_SKIP() << "Platform not supported";
#endif
}
