#include <gtest/gtest.h>

#if defined(_WIN32)

#include <winsock2.h>
#include <windows.h>
#include <chrono>
#include <cstring>
#include <stop_token>
#include "IoContext.h"
#include "Coroutine/IoAwaitable.h"
#include "Coroutine/Task.h"
#include "Engine/Iocp/IocpEngine.h"

using namespace ne;
using namespace ne::io;

namespace
{
	bool_t MakeConnectedPair(SOCKET& _a, SOCKET& _b)
	{
		const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listener == INVALID_SOCKET) return false;

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
		address.sin_port = 0;

		int_t length = static_cast<int_t>(sizeof(address));
		if (::bind(listener, reinterpret_cast<sockaddr*>(&address), length) != 0 ||
			::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0 ||
			::listen(listener, 1) != 0)
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

	struct WsaScope
	{
		WsaScope() noexcept { WSADATA data; ::WSAStartup(MAKEWORD(2, 2), &data); }
		~WsaScope() noexcept { ::WSACleanup(); }
	};

	ne::Task<IoResult<std::size_t>> ReceiveOp(IoContext& _context, const ulonglong_t _handle, void* _buffer, const std::size_t _length)
	{
		co_return co_await IoAwaitable{ _context, IoRequest{ .op = OpCode::Receive, .handle = _handle, .buffer = _buffer, .length = _length } };
	}

	ne::Task<IoResult<std::size_t>> ReceiveOpCancellable(IoContext& _context, const ulonglong_t _handle, void* _buffer, const std::size_t _length, std::stop_token _token)
	{
		co_return co_await IoAwaitable{ _context, IoRequest{ .op = OpCode::Receive, .handle = _handle, .buffer = _buffer, .length = _length }, std::move(_token) };
	}

	template <typename T>
	T Drive(IoContext& _context, ne::Task<T>& _task)
	{
		_task.Resume();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!_task.IsReady() && std::chrono::steady_clock::now() < deadline)
			(void)_context.RunOnce(std::chrono::milliseconds{ 50 });
		return _task.await_resume();
	}
}

// ── IoAwaitable 로 수신하면 IoResult<size_t> 로 바이트 수를 돌려준다 ──
TEST(IoAwaitableTest, ReceiveReturnsBytes)
{
	const WsaScope wsa;
	IocpEngine engine;
	IoContext context{ engine };

	SOCKET a = INVALID_SOCKET;
	SOCKET b = INVALID_SOCKET;
	ASSERT_TRUE(MakeConnectedPair(a, b));

	const char payload[] = "level2-awaitable";
	const std::size_t length = sizeof(payload) - 1;
	ASSERT_EQ(::send(a, payload, static_cast<int_t>(length), 0), static_cast<int_t>(length));

	char buffer[64]{};
	auto task = ReceiveOp(context, static_cast<ulonglong_t>(b), buffer, length);
	auto result = Drive(context, task);

	ASSERT_TRUE(result.IsOk()) << result.Error().What();
	EXPECT_EQ(result.Value(), length);
	EXPECT_EQ(std::memcmp(buffer, payload, length), 0);

	::closesocket(a);
	::closesocket(b);
}

// ── 진행 중 I/O 상태에서 코루틴 프레임이 파괴돼도 안전해야 한다(mid-flight, UAF 방지) ──
TEST(IoAwaitableTest, AbandonedInFlightIsSafe)
{
	const WsaScope wsa;
	IocpEngine engine;
	IoContext context{ engine };

	SOCKET a = INVALID_SOCKET;
	SOCKET b = INVALID_SOCKET;
	ASSERT_TRUE(MakeConnectedPair(a, b));

	char buffer[16]{};
	{
		// 데이터가 없어 WSARecv 는 pending — Resume 후 suspend 된다. 그 상태로 Task 를 파괴한다.
		auto task = ReceiveOp(context, static_cast<ulonglong_t>(b), buffer, sizeof(buffer));
		task.Resume();
	} // task 파괴 → IoAwaitable 소멸자가 handler->abandoned = true (heap handler 는 생존)

	// 소켓을 닫아 pending recv 를 완료(취소)시킨다 — 루프가 abandoned 완료를 안전하게 해제해야 한다.
	::closesocket(a);
	::closesocket(b);

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < deadline)
		if (!context.RunOnce(std::chrono::milliseconds{ 20 })) { /* 완료 없음 계속 */ }

	SUCCEED(); // 크래시/UAF 없이 여기 도달하면 성공
}

// ── stop_token 요청 시 진행 중 op 가 커널 취소되어 코루틴이 에러로 재개된다 ──
TEST(IoAwaitableTest, StopTokenCancelsInFlight)
{
	const WsaScope wsa;
	IocpEngine engine;
	IoContext context{ engine };

	SOCKET a = INVALID_SOCKET;
	SOCKET b = INVALID_SOCKET;
	ASSERT_TRUE(MakeConnectedPair(a, b));

	std::stop_source stopSource;
	char buffer[16]{};

	// 데이터가 없어 recv 는 pending — Resume 후 suspend, 취소 콜백 등록.
	auto task = ReceiveOpCancellable(context, static_cast<ulonglong_t>(b), buffer, sizeof(buffer), stopSource.get_token());
	task.Resume();

	stopSource.request_stop(); // → Cancel(handler) → CancelIoEx → op 은 aborted 로 완료된다

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!task.IsReady() && std::chrono::steady_clock::now() < deadline)
		(void)context.RunOnce(std::chrono::milliseconds{ 50 });

	ASSERT_TRUE(task.IsReady());
	auto result = task.await_resume();
	ASSERT_TRUE(result.IsError());
	EXPECT_EQ(result.Error().Code(), static_cast<ne::ulong_t>(ERROR_OPERATION_ABORTED));

	::closesocket(a);
	::closesocket(b);
}

#endif // _WIN32
