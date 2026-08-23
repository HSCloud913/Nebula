//
// Created by nebula on 24. 11. 3.
//

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "Base/Exception.h"
#include "Io/Context.h"
#include "Ipc/Pipe.h"

#if defined(IS_POSIX)
#   include "Io/Internal/Engine/Epoll/EpollEngine.h"
#elif defined(_WIN32)
#   include "Base/WinsockApi.h"
#   include "Io/Internal/Engine/Iocp/IocpEngine.h"
#endif

using ne::ipc::Pipe;

namespace
{
#if defined(IS_POSIX)
	using TestEngine = ne::io::EpollEngine;
#elif defined(_WIN32)
	using TestEngine = ne::io::IocpEngine;
#endif

	std::span<const std::byte> AsBytes(const std::string& _string) { return std::span(reinterpret_cast<const std::byte*>(_string.data()), _string.size()); }

	std::string AsString(const std::span<const std::byte> _bytes) { return std::string(reinterpret_cast<const ne::char_t*>(_bytes.data()), _bytes.size()); }

	// 완료 통지는 RunOnce() 를 호출한 스레드에서만 온다(IOCP·epoll 공통) — 그래서 테스트가 직접
	// 루프를 돌려야 코루틴이 재개된다.
	void DriveContext(ne::io::Context& _context, const std::atomic<bool>& _isDone, const std::chrono::milliseconds _timeout = std::chrono::seconds(5))
	{
		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while (!_isDone.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) (void)_context.RunOnce(std::chrono::milliseconds{ 10 });
	}

	// 서버/클라이언트가 붙은 파이프 한 쌍. Listen() 은 상대가 붙을 때까지 블로킹하므로 별도
	// 스레드에서 돌린 뒤 join 한다.
	struct ConnectedPipes
	{
		Pipe server;
		Pipe client;
	};

	[[nodiscard]] ConnectedPipes MakeConnectedPipes(const std::string& _name)
	{
		ConnectedPipes pipes{ Pipe(_name), Pipe(_name) };

		auto listenThread = std::thread([&pipes] { pipes.server.Listen(); });
		pipes.client.Connect();
		listenThread.join();

		return pipes;
	}
}



// ─── 동기 API ────────────────────────────────────────────────────────────────

TEST(PipeTest, ListenConnectReadWrite)
{
	auto pipes = MakeConnectedPipes("nebula-pipe-test-readwrite");

	const auto toServer = std::string("nebula-ipc-pipe-to-server");
	ASSERT_TRUE(pipes.client.Write(AsBytes(toServer)));

	auto serverBuffer = std::vector<std::byte>(64);
	const auto serverReceived = pipes.server.Read(serverBuffer);
	ASSERT_GE(serverReceived, 0);
	serverBuffer.resize(serverReceived);
	EXPECT_EQ(AsString(serverBuffer), toServer);

	const auto toClient = std::string("nebula-ipc-pipe-to-client");
	ASSERT_TRUE(pipes.server.Write(AsBytes(toClient)));

	auto clientBuffer = std::vector<std::byte>(64);
	const auto clientReceived = pipes.client.Read(clientBuffer);
	ASSERT_GE(clientReceived, 0);
	clientBuffer.resize(clientReceived);
	EXPECT_EQ(AsString(clientBuffer), toClient);
}



// ─── 비동기 API ──────────────────────────────────────────────────────────────

TEST(PipeTest, ReadAsyncReceivesWhatThePeerWrote)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	ne::io::Context context{ engine };

	auto pipes = MakeConnectedPipes("nebula-pipe-async-basic");

	const auto message = std::string("nebula-async-receive");
	std::atomic<bool> isDone{ false };
	auto received = ne::io::IoResult<std::size_t>::Ok(0);
	std::vector<std::byte> buffer(64);

	// 코루틴 람다는 이름 붙인 변수에 담고 나서 호출한다. 만들자마자 호출하면(IIFE) 코루틴
	// 프레임이 클로저를 참조로 들고 있는데 그 클로저가 임시객체라 즉시 소멸해, Resume() 시점에는
	// 이미 댕글링이다 — 크래시 없이 조용히 잘못된 메모리를 읽는다.
	auto readFn = [&]() -> ne::Task<void> {
		received = co_await pipes.server.ReadAsync(buffer, context);
		isDone.store(true, std::memory_order_release);
	};
	auto readTask = readFn();
	readTask.Resume();

	auto writer = std::thread([&pipes, &message] {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		(void)pipes.client.Write(AsBytes(message));
	});

	DriveContext(context, isDone);
	writer.join();

	ASSERT_TRUE(isDone.load()) << "ReadAsync timed out";
	ASSERT_TRUE(received.IsOk()) << received.Error().What();

	buffer.resize(received.Value());
	EXPECT_EQ(AsString(buffer), message);
}

TEST(PipeTest, ReadAsyncDoesNotBlockTheLoop)
{
	// 이 테스트가 보는 것은 "ReadAsync 가 제출만 하고 반환하는가" 다. 내부에서 블로킹한다면 A 를
	// Resume 한 시점에 이미 데이터를 기다리며 멈춰 있어 B 가 돌지 못한다.
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	ne::io::Context context{ engine };

	auto pipes = MakeConnectedPipes("nebula-pipe-async-nonblock");

	std::atomic<bool> isReadDone{ false };
	std::atomic<bool> isOtherDone{ false };
	auto readResult = ne::io::IoResult<std::size_t>::Ok(0);
	std::vector<std::byte> buffer(64);

	auto readFn = [&]() -> ne::Task<void> {
		readResult = co_await pipes.server.ReadAsync(buffer, context);
		isReadDone.store(true, std::memory_order_release);
	};
	auto readTask = readFn();
	readTask.Resume(); // 제출 후 즉시 반환(코루틴 suspend)

	auto otherFn = [&]() -> ne::Task<void> {
		isOtherDone.store(true, std::memory_order_release);
		co_return;
	};
	auto otherTask = otherFn();
	otherTask.Resume();

	EXPECT_TRUE(isOtherDone.load(std::memory_order_acquire)) << "ReadAsync 가 제출 대신 블로킹하면 이 태스크가 돌 수 없다";

	auto writer = std::thread([&pipes] {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		(void)pipes.client.Write(AsBytes(std::string("ping")));
	});

	DriveContext(context, isReadDone);
	writer.join();

	ASSERT_TRUE(isReadDone.load()) << "ReadAsync timed out";
	ASSERT_TRUE(readResult.IsOk()) << readResult.Error().What();

	buffer.resize(readResult.Value());
	EXPECT_EQ(AsString(buffer), "ping");
}

TEST(PipeTest, WriteAsyncSendsWhatThePeerReads)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	ne::io::Context context{ engine };

	auto pipes = MakeConnectedPipes("nebula-pipe-async-write");

	const auto message = std::string("nebula-async-send");
	std::atomic<bool> isDone{ false };
	auto writeResult = ne::io::IoResult<std::size_t>::Ok(0);

	auto writeFn = [&]() -> ne::Task<void> {
		writeResult = co_await pipes.client.WriteAsync(AsBytes(message), context);
		isDone.store(true, std::memory_order_release);
	};
	auto writeTask = writeFn();
	writeTask.Resume();

	DriveContext(context, isDone);

	ASSERT_TRUE(isDone.load()) << "WriteAsync timed out";
	ASSERT_TRUE(writeResult.IsOk()) << writeResult.Error().What();
	EXPECT_EQ(writeResult.Value(), message.size());

	// 받는 쪽은 다른 Pipe 객체이므로 "비동기 사용 후 동기 금지" 규칙에 걸리지 않는다.
	auto buffer = std::vector<std::byte>(64);
	const auto received = pipes.server.Read(buffer);
	ASSERT_GE(received, 0);
	buffer.resize(received);
	EXPECT_EQ(AsString(buffer), message);
}

TEST(PipeTest, RejectsSyncCallsAfterAsyncUse)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	ne::io::Context context{ engine };

	auto pipes = MakeConnectedPipes("nebula-pipe-async-then-sync");

	const auto message = std::string("x");
	std::atomic<bool> isDone{ false };

	auto writeFn = [&]() -> ne::Task<void> {
		(void)co_await pipes.client.WriteAsync(AsBytes(message), context);
		isDone.store(true, std::memory_order_release);
	};
	auto writeTask = writeFn();
	writeTask.Resume();

	DriveContext(context, isDone);
	ASSERT_TRUE(isDone.load());

	auto buffer = std::vector<std::byte>(16);

#if defined(_WIN32)
	// 같은 핸들의 완료가 IOCP 큐로 가기 시작했다 — 동기 Read 의 GetOverlappedResult 대기가
	// RunOnce() 와 완료를 두고 경합하므로, 조용히 엉뚱한 완료를 집는 대신 거부해야 한다.
	EXPECT_THROW((void)pipes.client.Read(buffer), ne::Exception);
	EXPECT_THROW((void)pipes.client.Write(AsBytes(message)), ne::Exception);
#else
	// POSIX 는 같은 fd 에 동기 recv 와 비동기 제출을 섞어도 되므로 막지 않는다.
	(void)buffer;
	SUCCEED() << "POSIX 는 동기/비동기 혼용을 제한하지 않는다";
#endif
}
