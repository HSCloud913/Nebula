#include <gtest/gtest.h>

#if defined(_WIN32)

#include "Base/WinsockApi.h"
#include <chrono>
#include <span>
#include "Io/Runtime.h"
#include "Io/Socket.h"
#include "Base/Coroutine/Task.h"

using namespace ne;
using namespace ne::io;

namespace
{
	struct WsaScope
	{
		WsaScope() noexcept
		{
			WSADATA data;
			::WSAStartup(MAKEWORD(2, 2), &data);
		}
		~WsaScope() noexcept { ::WSACleanup(); }
	};

	bool_t MakeConnectedPair(SOCKET& _a, SOCKET& _b)
	{
		const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listener == INVALID_SOCKET) return false;

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
		address.sin_port = 0;

		int_t length = static_cast<int_t>(sizeof(address));
		if (::bind(listener, reinterpret_cast<sockaddr*>(&address), length) != 0 || ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0 || ::listen(listener, 1) != 0)
		{
			::closesocket(listener);
			return false;
		}

		_a = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (_a == INVALID_SOCKET || ::connect(_a, reinterpret_cast<sockaddr*>(&address), static_cast<int_t>(sizeof(address))) != 0)
		{
			::closesocket(listener);
			if (_a != INVALID_SOCKET) ::closesocket(_a);
			return false;
		}

		_b = ::accept(listener, nullptr, nullptr);
		::closesocket(listener);
		return _b != INVALID_SOCKET;
	}

	ne::Task<int_t> ComputeAfterSleep(Context& _context)
	{
		co_await _context.SleepFor(std::chrono::milliseconds(20));
		co_return 42;
	}

	ne::Task<void_t> SleepOnly(Context& _context)
	{
		co_await _context.SleepFor(std::chrono::milliseconds(10));
		co_return;
	}
}

// ── Runtime: 플랫폼 엔진이 정상 초기화됨 ──
TEST(RuntimeTest, IsValid)
{
	const WsaScope wsa;
	Runtime runtime; // 기본 PROACTOR(IOCP)
	EXPECT_TRUE(runtime.IsValid());
}

// ── Runtime: BlockOn 이 타이머 코루틴을 완료까지 구동하고 값을 반환 ──
TEST(RuntimeTest, BlockOnReturnsValue)
{
	const WsaScope wsa;
	Runtime runtime;
	ASSERT_TRUE(runtime.IsValid());

	const int_t value = runtime.BlockOn(ComputeAfterSleep(runtime.GetContext()));
	EXPECT_EQ(value, 42);
}

// ── Runtime: void 반환 코루틴도 BlockOn 으로 완료까지 구동 ──
TEST(RuntimeTest, BlockOnVoid)
{
	const WsaScope wsa;
	Runtime runtime;
	ASSERT_TRUE(runtime.IsValid());

	runtime.BlockOn(SleepOnly(runtime.GetContext())); // 완료까지 블록되면 통과
	SUCCEED();
}

// ── Runtime: 실제 소켓 I/O(엔진 완료)를 BlockOn 이 구동 ──
TEST(RuntimeTest, BlockOnDrivesSocketIo)
{
	const WsaScope wsa;
	Runtime runtime;
	ASSERT_TRUE(runtime.IsValid());
	Context& context = runtime.GetContext();

	SOCKET rawA = INVALID_SOCKET;
	SOCKET rawB = INVALID_SOCKET;
	ASSERT_TRUE(MakeConnectedPair(rawA, rawB));
	auto adoptedA = Socket::Attach(static_cast<socket_t>(rawA), context);
	auto adoptedB = Socket::Attach(static_cast<socket_t>(rawB), context);
	ASSERT_TRUE(adoptedA.IsOk());
	ASSERT_TRUE(adoptedB.IsOk());
	Socket sender = std::move(adoptedA.Value());
	Socket receiver = std::move(adoptedB.Value());

	const char payload[] = "hi";
	auto sendResult = runtime.BlockOn(sender.Send(std::span<const ne::byte_t>{ reinterpret_cast<const ne::byte_t*>(payload), 2 }));
	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();

	ne::byte_t buffer[8]{};
	auto recvResult = runtime.BlockOn(receiver.Receive(std::span<ne::byte_t>{ buffer, sizeof(buffer) }));
	ASSERT_TRUE(recvResult.IsOk()) << recvResult.Error().What();
	EXPECT_EQ(recvResult.Value(), 2u);
	EXPECT_EQ(buffer[0], static_cast<ne::byte_t>('h'));
	EXPECT_EQ(buffer[1], static_cast<ne::byte_t>('i'));
}

// ── Socket 으로 리스너를 세우고(Bind+Listen) LocalPort 조회 → Accept 로 연결 수락 ──
TEST(RuntimeTest, ListeningSocketAcceptsConnection)
{
	const WsaScope wsa;
	Runtime runtime;
	ASSERT_TRUE(runtime.IsValid());
	Context& context = runtime.GetContext();

	auto created = Socket::Create(context, AF_INET);
	ASSERT_TRUE(created.IsOk()) << created.Error().What();
	Socket listener = std::move(created.Value());

	ASSERT_TRUE(listener.Bind("127.0.0.1", 0).IsOk()); // 임시 포트
	ASSERT_TRUE(listener.Listen(1).IsOk());

	const uint16_t port = listener.LocalPort();
	ASSERT_NE(port, 0);

	auto clientCreate = Socket::Create(context, AF_INET);
	ASSERT_TRUE(clientCreate.IsOk()) << clientCreate.Error().What();
	Socket client = std::move(clientCreate.Value());

	// Accept 와 Connect 는 상호 의존 — 둘 다 제출하고 함께 구동한다.
	auto acceptTask = listener.Accept();
	auto connectTask = client.Connect("127.0.0.1", port);
	acceptTask.Resume();
	connectTask.Resume();

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while ((!acceptTask.IsReady() || !connectTask.IsReady()) && std::chrono::steady_clock::now() < deadline) (void_t)context.RunOnce(std::chrono::milliseconds{ 50 });

	ASSERT_TRUE(acceptTask.IsReady());
	ASSERT_TRUE(connectTask.IsReady());

	auto acceptResult = acceptTask.await_resume();
	auto connectResult = connectTask.await_resume();
	ASSERT_TRUE(acceptResult.IsOk()) << acceptResult.Error().What();
	ASSERT_TRUE(connectResult.IsOk()) << connectResult.Error().What();
	EXPECT_TRUE(acceptResult.Value().IsValid());
}

#endif // _WIN32
