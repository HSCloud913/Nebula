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

#include "Io/Context.h"
#include "Ipc/MessageQueue.h"

#if defined(IS_POSIX)
#   include "Io/Internal/Engine/Epoll/EpollEngine.h"
#elif defined(_WIN32)
#   include "Base/WinsockApi.h"
#   include "Io/Internal/Engine/Iocp/IocpEngine.h"
#endif

using ne::ipc::MessageQueue;

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

	struct ConnectedQueues
	{
		MessageQueue server;
		MessageQueue client;
	};

	[[nodiscard]] ConnectedQueues MakeConnectedQueues(const std::string& _name)
	{
		ConnectedQueues queues{ MessageQueue(_name), MessageQueue(_name) };

		auto listenThread = std::thread([&queues] { queues.server.Listen(); });
		queues.client.Connect();
		listenThread.join();

		return queues;
	}
}



// ─── 동기 API ────────────────────────────────────────────────────────────────

TEST(MessageQueueTest, SendReceiveRoundTrip)
{
	auto queues = MakeConnectedQueues("nebula-mq-test-roundtrip");

	const auto message = std::string("nebula-ipc-message-queue");
	queues.client.Send(AsBytes(message));

	EXPECT_EQ(AsString(queues.server.Receive()), message);
}

TEST(MessageQueueTest, PreservesMessageBoundaries)
{
	// 이것이 MessageQueue 가 Pipe 와 다른 유일한 이유다 — 두 번 보낸 것이 한 번에 붙어 오면
	// 호출자가 직접 길이 프레이밍을 해야 하고, 그러면 이 클래스는 존재할 이유가 없다.
	auto queues = MakeConnectedQueues("nebula-mq-test-boundaries");

	queues.client.Send(AsBytes(std::string("first")));
	queues.client.Send(AsBytes(std::string("second")));

	EXPECT_EQ(AsString(queues.server.Receive()), "first");
	EXPECT_EQ(AsString(queues.server.Receive()), "second");
}



// ─── 비동기 API ──────────────────────────────────────────────────────────────

TEST(MessageQueueTest, ReceiveAsyncReceivesWhatThePeerSent)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	ne::io::Context context{ engine };

	auto queues = MakeConnectedQueues("nebula-mq-async-basic");

	const auto message = std::string("nebula-async-receive");
	std::atomic<bool> isDone{ false };
	auto received = ne::io::IoResult<std::vector<std::byte>>::Ok({});

	// 코루틴 람다는 이름 붙인 변수에 담고 나서 호출한다 — 만들자마자 호출하면(IIFE) 코루틴 프레임이
	// 참조로 들고 있는 클로저가 임시객체라 즉시 소멸해, Resume() 시점에는 이미 댕글링이다.
	auto receiveFn = [&]() -> ne::Task<void> {
		received = co_await queues.server.ReceiveAsync(context);
		isDone.store(true, std::memory_order_release);
	};
	auto receiveTask = receiveFn();
	receiveTask.Resume();

	auto sender = std::thread([&queues, &message] {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		queues.client.Send(AsBytes(message));
	});

	DriveContext(context, isDone);
	sender.join();

	ASSERT_TRUE(isDone.load()) << "ReceiveAsync timed out";
	ASSERT_TRUE(received.IsOk()) << received.Error().What();
	EXPECT_EQ(AsString(received.Value()), message);
}

TEST(MessageQueueTest, ReceiveAsyncDoesNotBlockTheLoop)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	ne::io::Context context{ engine };

	auto queues = MakeConnectedQueues("nebula-mq-async-nonblock");

	std::atomic<bool> isReceiveDone{ false };
	std::atomic<bool> isOtherDone{ false };
	auto receiveResult = ne::io::IoResult<std::vector<std::byte>>::Ok({});

	auto receiveFn = [&]() -> ne::Task<void> {
		receiveResult = co_await queues.server.ReceiveAsync(context);
		isReceiveDone.store(true, std::memory_order_release);
	};
	auto receiveTask = receiveFn();
	receiveTask.Resume(); // 제출 후 즉시 반환(코루틴 suspend)

	auto otherFn = [&]() -> ne::Task<void> {
		isOtherDone.store(true, std::memory_order_release);
		co_return;
	};
	auto otherTask = otherFn();
	otherTask.Resume();

	EXPECT_TRUE(isOtherDone.load(std::memory_order_acquire)) << "ReceiveAsync 가 제출 대신 블로킹하면 이 태스크가 돌 수 없다";

	auto sender = std::thread([&queues] {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		queues.client.Send(AsBytes(std::string("ping")));
	});

	DriveContext(context, isReceiveDone);
	sender.join();

	ASSERT_TRUE(isReceiveDone.load()) << "ReceiveAsync timed out";
	ASSERT_TRUE(receiveResult.IsOk()) << receiveResult.Error().What();
	EXPECT_EQ(AsString(receiveResult.Value()), "ping");
}

TEST(MessageQueueTest, SendAsyncSendsWhatThePeerReceives)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	ne::io::Context context{ engine };

	auto queues = MakeConnectedQueues("nebula-mq-async-send");

	const auto message = std::string("nebula-async-send");
	std::atomic<bool> isDone{ false };
	auto sendResult = ne::io::IoResult<ne::void_t>::Ok();

	auto sendFn = [&]() -> ne::Task<void> {
		sendResult = co_await queues.client.SendAsync(AsBytes(message), context);
		isDone.store(true, std::memory_order_release);
	};
	auto sendTask = sendFn();
	sendTask.Resume();

	DriveContext(context, isDone);

	ASSERT_TRUE(isDone.load()) << "SendAsync timed out";
	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();

	EXPECT_EQ(AsString(queues.server.Receive()), message);
}

TEST(MessageQueueTest, AsyncPreservesMessageBoundaries)
{
	// 비동기 경로도 동기 경로와 같은 보장을 해야 한다 — 여기서 경계가 무너지면 RequestKind 를
	// 잘못 골랐거나(POSIX 에서 SOCK_STREAM 처럼 동작) 버퍼를 너무 작게 잡은 것이다.
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	ne::io::Context context{ engine };

	auto queues = MakeConnectedQueues("nebula-mq-async-boundaries");

	queues.client.Send(AsBytes(std::string("first")));
	queues.client.Send(AsBytes(std::string("second")));

	std::atomic<bool> isDone{ false };
	std::vector<std::string> messages;

	auto receiveFn = [&]() -> ne::Task<void> {
		for (int index = 0; index < 2; ++index)
		{
			auto result = co_await queues.server.ReceiveAsync(context);
			if (result.IsError()) break;

			messages.push_back(AsString(result.Value()));
		}

		isDone.store(true, std::memory_order_release);
	};
	auto receiveTask = receiveFn();
	receiveTask.Resume();

	DriveContext(context, isDone);

	ASSERT_TRUE(isDone.load()) << "ReceiveAsync timed out";
	ASSERT_EQ(messages.size(), 2u);
	EXPECT_EQ(messages[0], "first");
	EXPECT_EQ(messages[1], "second");
}
