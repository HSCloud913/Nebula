//
// Created by nebula on 24. 11. 3.
//

#include <gtest/gtest.h>
#include <atomic>
// #include <chrono>
// #include <string>
// #include <thread>

// #include "../../Ipc/MessageQueue.h"
//
// #if defined(IS_POSIX)
// #   include "Engine/Epoll/EpollEngine.h"
//     using TestIoEngine = ne::io::EpollEngine;
// #elif defined(_WIN32)
//
// #   include "Engine/Iocp/IocpEngine.h"
//     using TestIoEngine = ne::io::IocpEngine;
// #endif
//
// using ne::ipc::MessageQueue;
//
// namespace
// {
// 	std::span<const std::byte> AsBytes(const std::string& _string)
// 	{
// 		return std::span(reinterpret_cast<const std::byte*>(_string.data()), _string.size());
// 	}
//
// 	std::string AsString(const std::span<const std::byte> _bytes)
// 	{
// 		return std::string(reinterpret_cast<const ne::char_t*>(_bytes.data()), _bytes.size());
// 	}
//
// 	// 최대 5 초 동안 IIoEngine::RunOnce() 를 구동해 _flag 가 true 가 되길 대기.
// 	// POSIX(epoll/io_uring)·Windows(IOCP) 모두 완료 통지는 RunOnce() 호출 스레드에서만
// 	// 온다 — 두 플랫폼 다 이 방식으로 이벤트 루프를 돌려야 코루틴이 재개된다.
// 	void DriveEngine(TestIoEngine& _engine, const std::atomic<bool>& _flag,
// 	                  const std::chrono::milliseconds _timeout = std::chrono::seconds(5))
// 	{
// 		const auto deadline = std::chrono::steady_clock::now() + _timeout;
// 		while (!_flag.load(std::memory_order_acquire) &&
// 		       std::chrono::steady_clock::now() < deadline)
// 			(void)_engine.RunOnce(10);
// 	}
// }
//
// // ─── 기존 동기 테스트 (namespace 변경 외 회귀 없음) ──────────────────────────
//
// TEST(MessageQueueTest, SendReceiveRoundTrip)
// {
// 	auto server = MessageQueue("nebula-mq-test-roundtrip");
// 	auto serverThread = std::thread([&server] { server.Listen(); });
//
// 	auto client = MessageQueue("nebula-mq-test-roundtrip");
// 	client.Connect();
// 	serverThread.join();
//
// 	const auto message = std::string("nebula-ipc-message-queue");
// 	client.Send(AsBytes(message));
//
// 	const auto received = server.Receive();
// 	EXPECT_EQ(AsString(received), message);
// }
//
// TEST(MessageQueueTest, PreservesMessageBoundaries)
// {
// 	auto server = MessageQueue("nebula-mq-test-boundaries");
// 	auto serverThread = std::thread([&server] { server.Listen(); });
//
// 	auto client = MessageQueue("nebula-mq-test-boundaries");
// 	client.Connect();
// 	serverThread.join();
//
// 	client.Send(AsBytes(std::string("first")));
// 	client.Send(AsBytes(std::string("second")));
//
// 	EXPECT_EQ(AsString(server.Receive()), "first");
// 	EXPECT_EQ(AsString(server.Receive()), "second");
// }
//
// // ─── 비동기 테스트 ────────────────────────────────────────────────────────────
//
// // ReceiveAsync 가 IIoEngine 기반으로 정상 수신하는지 검증.
// //   - 서버: ReceiveAsync 로 대기 (코루틴 suspend)
// //   - 클라이언트 스레드: Connect() 후 동기 Send
// //   - 이벤트 루프 구동: RunOnce() 를 돌려야 완료가 코루틴으로 전달됨 (POSIX/Windows 공통)
// TEST(MessageQueueTest, ReceiveAsyncBasic)
// {
// #if defined(IS_POSIX) || defined(_WIN32)
// 	TestIoEngine engine;
//
// 	auto server = MessageQueue("nebula-mq-async-basic");
// 	auto serverListenThread = std::thread([&server] { server.Listen(); });
//
// 	auto client = MessageQueue("nebula-mq-async-basic");
// 	client.Connect();
// 	serverListenThread.join();
//
// 	const auto message = std::string("nebula-async-receive");
//
// 	std::atomic<bool> done{ false };
// 	ne::Result<std::vector<std::byte>, ne::OsError> received =
// 		ne::Result<std::vector<std::byte>, ne::OsError>::Ok({});
//
// 	auto task = [&]() -> ne::Task<void>
// 	{
// 		received = co_await server.ReceiveAsync(engine);
// 		done.store(true, std::memory_order_release);
// 	}();
// 	task.Resume();
//
// 	// 클라이언트에서 메시지 전송
// 	std::thread([&client, &message]
// 	{
// 		std::this_thread::sleep_for(std::chrono::milliseconds(10));
// 		client.Send(AsBytes(message));
// 	}).detach();
//
// 	DriveEngine(engine, done);
//
// 	ASSERT_TRUE(done.load()) << "ReceiveAsync timed out";
// 	ASSERT_TRUE(received.IsOk()) << received.Error().What();
// 	EXPECT_EQ(AsString(received.Value()), message);
// #else
// 	GTEST_SKIP() << "Platform not supported";
// #endif
// }
//
// // ReceiveAsync 대기 중 이벤트 루프가 다른 작업을 처리할 수 있음을 검증.
// //   - 코루틴 A: ReceiveAsync 로 suspend
// //   - 코루틴 B: 즉시 완료하는 카운터 증가
// //   - A 가 suspend 된 사이에 B 가 실행됐는지 확인 (비동기성 증명)
// TEST(MessageQueueTest, ReceiveAsyncNonBlocking)
// {
// #if defined(IS_POSIX) || defined(_WIN32)
// 	TestIoEngine engine;
//
// 	auto server = MessageQueue("nebula-mq-async-nonblock");
// 	auto serverListenThread = std::thread([&server] { server.Listen(); });
//
// 	auto client = MessageQueue("nebula-mq-async-nonblock");
// 	client.Connect();
// 	serverListenThread.join();
//
// 	std::atomic<bool> aDone{ false };
// 	std::atomic<bool> bDone{ false };
// 	ne::Result<std::vector<std::byte>, ne::OsError> aResult =
// 		ne::Result<std::vector<std::byte>, ne::OsError>::Ok({});
//
// 	// A: 메시지 도착까지 suspend
// 	auto taskA = [&]() -> ne::Task<void>
// 	{
// 		aResult = co_await server.ReceiveAsync(engine);
// 		aDone.store(true, std::memory_order_release);
// 	}();
// 	taskA.Resume(); // I/O 제출 후 즉시 반환 (코루틴 suspend)
//
// 	// B: suspend 없이 즉시 완료
// 	auto taskB = [&]() -> ne::Task<void>
// 	{
// 		bDone.store(true, std::memory_order_release);
// 		co_return;
// 	}();
// 	taskB.Resume();
//
// 	// B 는 A 가 suspend 된 사이에 실행돼야 함
// 	EXPECT_TRUE(bDone.load(std::memory_order_acquire))
// 		<< "B must run while A is suspended";
//
// 	// 클라이언트에서 메시지 전송
// 	std::thread([&client]
// 	{
// 		std::this_thread::sleep_for(std::chrono::milliseconds(10));
// 		client.Send(AsBytes(std::string("ping")));
// 	}).detach();
//
// 	DriveEngine(engine, aDone);
//
// 	ASSERT_TRUE(aDone.load()) << "ReceiveAsync timed out";
// 	ASSERT_TRUE(aResult.IsOk()) << aResult.Error().What();
// 	EXPECT_EQ(AsString(aResult.Value()), "ping");
// #else
// 	GTEST_SKIP() << "Platform not supported";
// #endif
// }
//
// // SendAsync 가 메시지를 전송하고 상대방이 동기 Receive 로 수신하는지 검증
// TEST(MessageQueueTest, SendAsyncBasic)
// {
// #if defined(IS_POSIX) || defined(_WIN32)
// 	TestIoEngine engine;
//
// 	auto server = MessageQueue("nebula-mq-async-send");
// 	auto serverListenThread = std::thread([&server] { server.Listen(); });
//
// 	auto client = MessageQueue("nebula-mq-async-send");
// 	client.Connect();
// 	serverListenThread.join();
//
// 	const auto message = std::string("nebula-async-send");
//
// 	std::atomic<bool> done{ false };
// 	ne::Result<void, ne::OsError> sendResult = ne::Result<void, ne::OsError>::Ok();
//
// 	auto task = [&]() -> ne::Task<void>
// 	{
// 		sendResult = co_await client.SendAsync(AsBytes(message), engine);
// 		done.store(true, std::memory_order_release);
// 	}();
// 	task.Resume();
//
// 	DriveEngine(engine, done);
//
// 	ASSERT_TRUE(done.load()) << "SendAsync timed out";
// 	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();
//
// 	const auto received = server.Receive();
// 	EXPECT_EQ(AsString(received), message);
// #else
// 	GTEST_SKIP() << "Platform not supported";
// #endif
// }
