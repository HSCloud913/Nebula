#include <gtest/gtest.h>
#include "Type.h" // IS_POSIX / 플랫폼 매크로 정의 — 아래 #if/#elif 분기 전에 반드시 포함

#if defined(_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <chrono>
#include <cstring>
#include "Engine/Iocp/IocpEngine.h"

using namespace ne;
using namespace ne::io;

namespace
{
	// userData 로 특정 완료 하나를 기다려 result 를 돌려준다(없으면 -1).
	longlong_t WaitFor(IocpEngine& _engine, void* _tag, const std::chrono::milliseconds _timeout = std::chrono::seconds(5))
	{
		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			IoCompletion completions[8];
			const int_t count = _engine.WaitCompletions(completions, 8, std::chrono::milliseconds(50));
			for (int_t i = 0; i < count; ++i)
				if (completions[i].userData == _tag) return completions[i].result;
		}

		return -1;
	}

	// 127.0.0.1 로 연결된 소켓 쌍(블로킹) 생성.
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
}

// ── 파일 Write → Read 왕복 (완료 기반 Submit/WaitCompletions) ──
TEST(IoEngineTest, FileWriteThenReadRoundTrip)
{
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());

	const lpcstr_t path = "test_engine_file.bin";
	const HANDLE file = ::CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_OVERLAPPED, nullptr);
	ASSERT_NE(file, INVALID_HANDLE_VALUE);

	const char payload[] = "iocp-completion-model";
	const std::size_t length = sizeof(payload) - 1;

	int_t writeTag = 0;
	IoRequest write{ .op = OpCode::Write, .userData = &writeTag, .handle = reinterpret_cast<ulonglong_t>(file),
	                 .buffer = const_cast<lpstr_t>(payload), .length = length, .offset = 0 };
	engine.Submit(write);
	EXPECT_EQ(WaitFor(engine, &writeTag), static_cast<longlong_t>(length));

	char readBuffer[64]{};
	int_t readTag = 0;
	IoRequest read{ .op = OpCode::Read, .userData = &readTag, .handle = reinterpret_cast<ulonglong_t>(file),
	                .buffer = readBuffer, .length = length, .offset = 0 };
	engine.Submit(read);
	EXPECT_EQ(WaitFor(engine, &readTag), static_cast<longlong_t>(length));
	EXPECT_EQ(std::memcmp(readBuffer, payload, length), 0);

	::CloseHandle(file);
	::DeleteFileA(path);
}

// ── 소켓 Send → Receive 왕복 ──
TEST(IoEngineTest, SocketSendThenReceiveRoundTrip)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());

	SOCKET a = INVALID_SOCKET;
	SOCKET b = INVALID_SOCKET;
	ASSERT_TRUE(MakeConnectedPair(a, b));

	const char payload[] = "engine-socket-roundtrip";
	const std::size_t length = sizeof(payload) - 1;

	int_t sendTag = 0;
	IoRequest send{ .op = OpCode::Send, .userData = &sendTag, .handle = static_cast<ulonglong_t>(a),
	                .buffer = const_cast<lpstr_t>(payload), .length = length };
	engine.Submit(send);
	EXPECT_EQ(WaitFor(engine, &sendTag), static_cast<longlong_t>(length));

	char receiveBuffer[64]{};
	int_t receiveTag = 0;
	IoRequest receive{ .op = OpCode::Receive, .userData = &receiveTag, .handle = static_cast<ulonglong_t>(b),
	                   .buffer = receiveBuffer, .length = length };
	engine.Submit(receive);
	EXPECT_EQ(WaitFor(engine, &receiveTag), static_cast<longlong_t>(length));
	EXPECT_EQ(std::memcmp(receiveBuffer, payload, length), 0);

	::closesocket(a);
	::closesocket(b);
}

// ── Capability 매트릭스 (스펙 2.2) ──
TEST(IoEngineTest, SupportsMatrix)
{
	const IocpEngine engine;
	EXPECT_TRUE(engine.Supports(Capability::SendFileZeroCopy));
	EXPECT_TRUE(engine.Supports(Capability::SendMemZeroCopy));
	EXPECT_TRUE(engine.Supports(Capability::RecvOverheadReduced));
	EXPECT_FALSE(engine.Supports(Capability::RecvTrueZeroCopy));
}

// ── Wake 가 완료 없이도 대기를 즉시 해제 ──
TEST(IoEngineTest, WakeUnblocksWait)
{
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());

	engine.Wake();

	const auto start = std::chrono::steady_clock::now();
	IoCompletion completions[4];
	const int_t count = engine.WaitCompletions(completions, 4, std::chrono::seconds(5));
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

	EXPECT_EQ(count, 0);                 // Wake 는 완료를 만들지 않는다(대기만 해제)
	EXPECT_LT(elapsed, 1000);            // 5s 타임아웃까지 블록하지 않고 즉시 풀려야 한다
}

#elif defined(IS_POSIX)

#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include "Engine/Epoll/EpollEngine.h"
#include "Engine/IoUring/IoUringEngine.h"

using namespace ne;
using namespace ne::io;

namespace
{
	longlong_t WaitFor(IIoEngine& _engine, void* _tag, const std::chrono::milliseconds _timeout = std::chrono::seconds(5))
	{
		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			IoCompletion completions[8];
			const int_t count = _engine.WaitCompletions(completions, 8, std::chrono::milliseconds(50));
			for (int_t i = 0; i < count; ++i)
				if (completions[i].userData == _tag) return completions[i].result;
		}
		return -1;
	}

	// 임시 파일 Write → Read 왕복 (엔진 무관 — Submit/WaitCompletions 계약만 사용).
	void_t RunFileRoundTrip(IIoEngine& _engine)
	{
		const lpcstr_t path = "test_engine_posix_file.bin";
		const int_t fd = ::open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
		ASSERT_GE(fd, 0);

		const char payload[] = "posix-completion-model";
		const std::size_t length = sizeof(payload) - 1;

		int_t writeTag = 0;
		IoRequest write{ .op = OpCode::Write, .userData = &writeTag, .handle = static_cast<ulonglong_t>(fd),
		                 .buffer = const_cast<char*>(payload), .length = length, .offset = 0 };
		_engine.Submit(write);
		EXPECT_EQ(WaitFor(_engine, &writeTag), static_cast<longlong_t>(length));

		char buffer[64]{};
		int_t readTag = 0;
		IoRequest read{ .op = OpCode::Read, .userData = &readTag, .handle = static_cast<ulonglong_t>(fd),
		                .buffer = buffer, .length = length, .offset = 0 };
		_engine.Submit(read);
		EXPECT_EQ(WaitFor(_engine, &readTag), static_cast<longlong_t>(length));
		EXPECT_EQ(std::memcmp(buffer, payload, length), 0);

		::close(fd);
		::unlink(path);
	}

	// socketpair Send → Receive 왕복.
	void_t RunSocketRoundTrip(IIoEngine& _engine)
	{
		int_t pair[2] = { -1, -1 };
		ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
		(void)::fcntl(pair[0], F_SETFL, O_NONBLOCK);
		(void)::fcntl(pair[1], F_SETFL, O_NONBLOCK);

		const char payload[] = "posix-socket-roundtrip";
		const std::size_t length = sizeof(payload) - 1;

		int_t sendTag = 0;
		IoRequest send{ .op = OpCode::Send, .userData = &sendTag, .handle = static_cast<ulonglong_t>(pair[0]),
		                .buffer = const_cast<char*>(payload), .length = length };
		_engine.Submit(send);
		EXPECT_EQ(WaitFor(_engine, &sendTag), static_cast<longlong_t>(length));

		char buffer[64]{};
		int_t receiveTag = 0;
		IoRequest receive{ .op = OpCode::Receive, .userData = &receiveTag, .handle = static_cast<ulonglong_t>(pair[1]),
		                   .buffer = buffer, .length = length };
		_engine.Submit(receive);
		EXPECT_EQ(WaitFor(_engine, &receiveTag), static_cast<longlong_t>(length));
		EXPECT_EQ(std::memcmp(buffer, payload, length), 0);

		::close(pair[0]);
		::close(pair[1]);
	}
}

TEST(EpollEngineTest, FileRoundTrip)   { EpollEngine engine;   ASSERT_TRUE(engine.IsValid()); RunFileRoundTrip(engine); }
TEST(EpollEngineTest, SocketRoundTrip) { EpollEngine engine;   ASSERT_TRUE(engine.IsValid()); RunSocketRoundTrip(engine); }
TEST(EpollEngineTest, Supports)
{
	const EpollEngine engine;
	EXPECT_TRUE(engine.Supports(Capability::SendFileZeroCopy));
	EXPECT_FALSE(engine.Supports(Capability::RecvTrueZeroCopy));
}

// io_uring 은 커널(<5.1)/컨테이너 seccomp 로 막힐 수 있다 — 그 경우 SKIP(EpollEngine 폴백이 담당).
// 전체 io_uring 런타임 검증은 `docker run --security-opt seccomp=unconfined` 로.
TEST(IoUringEngineTest, FileRoundTrip)   { IoUringEngine engine; if (!engine.IsValid()) GTEST_SKIP() << "io_uring unavailable"; RunFileRoundTrip(engine); }
TEST(IoUringEngineTest, SocketRoundTrip) { IoUringEngine engine; if (!engine.IsValid()) GTEST_SKIP() << "io_uring unavailable"; RunSocketRoundTrip(engine); }

#endif // platform

