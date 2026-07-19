#include <gtest/gtest.h>
#include "Base/Type.h" // IS_POSIX / 플랫폼 매크로 정의 — 아래 #if/#elif 분기 전에 반드시 포함

#if defined(_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <chrono>
#include <cstring>
#include "Io/Engine/Iocp/IocpEngine.h"

using namespace ne;
using namespace ne::io;

namespace
{
	// userData 로 특정 완료 하나를 기다려 result 를 돌려준다(없으면 -1).
	longlong_t WaitFor(IocpEngine& _engine, void_t* _tag, const std::chrono::milliseconds _timeout = std::chrono::seconds(5))
	{
		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			Completion completions[8];
			const int_t count = _engine.WaitCompletions(completions, 8, std::chrono::milliseconds(50));
			for (int_t i = 0; i < count; ++i) if (completions[i].userData == _tag) return completions[i].result;
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

	struct WsaScope
	{
		WsaScope() noexcept
		{
			WSADATA data;
			::WSAStartup(MAKEWORD(2, 2), &data);
		}
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
	Request write{ .requestKind = RequestKind::WRITE, .userData = &writeTag, .handle = reinterpret_cast<ulonglong_t>(file), .buffer = const_cast<lpstr_t>(payload), .length = length, .offset = 0 };
	engine.Submit(write);
	EXPECT_EQ(WaitFor(engine, &writeTag), static_cast<longlong_t>(length));

	char readBuffer[64]{};
	int_t readTag = 0;
	Request read{ .requestKind = RequestKind::READ, .userData = &readTag, .handle = reinterpret_cast<ulonglong_t>(file), .buffer = readBuffer, .length = length, .offset = 0 };
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
	Request send{ .requestKind = RequestKind::SEND, .userData = &sendTag, .handle = static_cast<ulonglong_t>(a), .buffer = const_cast<lpstr_t>(payload), .length = length };
	engine.Submit(send);
	EXPECT_EQ(WaitFor(engine, &sendTag), static_cast<longlong_t>(length));

	char receiveBuffer[64]{};
	int_t receiveTag = 0;
	Request receive{ .requestKind = RequestKind::RECEIVE, .userData = &receiveTag, .handle = static_cast<ulonglong_t>(b), .buffer = receiveBuffer, .length = length };
	engine.Submit(receive);
	EXPECT_EQ(WaitFor(engine, &receiveTag), static_cast<longlong_t>(length));
	EXPECT_EQ(std::memcmp(receiveBuffer, payload, length), 0);

	::closesocket(a);
	::closesocket(b);
}

// ── SendFile(TransmitFile) — 파일 → 소켓 zero-copy 전송 ──
TEST(IoEngineTest, SendFileZeroCopyRoundTrip)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());

	const lpcstr_t path = "test_engine_sendfile.bin";
	const char payload[] = "iocp-sendfile-zerocopy";
	const std::size_t length = sizeof(payload) - 1;
	{
		const HANDLE file = ::CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		ASSERT_NE(file, INVALID_HANDLE_VALUE);
		ulong_t written = 0;
		ASSERT_TRUE(::WriteFile(file, payload, static_cast<ulong_t>(length), &written, nullptr));
		::CloseHandle(file);
	}

	const HANDLE sourceFile = ::CreateFileA(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
	ASSERT_NE(sourceFile, INVALID_HANDLE_VALUE);

	SOCKET a = INVALID_SOCKET;
	SOCKET b = INVALID_SOCKET;
	ASSERT_TRUE(MakeConnectedPair(a, b));

	int_t sendFileTag = 0;
	Request sendFile{ .requestKind = RequestKind::SEND_FILE, .userData = &sendFileTag, .handle = static_cast<ulonglong_t>(a), .length = length, .offset = 0, .auxHandle = reinterpret_cast<ulonglong_t>(sourceFile) };
	engine.Submit(sendFile);
	EXPECT_EQ(WaitFor(engine, &sendFileTag), static_cast<longlong_t>(length));

	char receiveBuffer[64]{};
	int_t receiveTag = 0;
	Request receive{ .requestKind = RequestKind::RECEIVE, .userData = &receiveTag, .handle = static_cast<ulonglong_t>(b), .buffer = receiveBuffer, .length = length };
	engine.Submit(receive);
	EXPECT_EQ(WaitFor(engine, &receiveTag), static_cast<longlong_t>(length));
	EXPECT_EQ(std::memcmp(receiveBuffer, payload, length), 0);

	::CloseHandle(sourceFile);
	::closesocket(a);
	::closesocket(b);
	::DeleteFileA(path);
}

// ── SendZeroCopy(RIO) — 등록 버퍼로 메모리 → 소켓 zero-copy 전송 ──
TEST(IoEngineTest, SendZeroCopyRegisteredBufferRoundTrip)
{
	const WsaScope wsa;
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());

	IRegisteredBufferProvider* provider = engine.AsRegisteredBufferProvider();
	ASSERT_NE(provider, nullptr);

	char region[64] = "iocp-rio-zerocopy-send";
	const std::size_t length = std::strlen(region);
	auto registered = provider->RegisterBuffer(std::span<ne::byte_t>{ reinterpret_cast<ne::byte_t*>(region), sizeof(region) });
	ASSERT_TRUE(registered.IsOk()) << registered.Error().What();
	const BufferHandle handle = registered.Value();

	// RIOSend 를 호출할 소켓(a)은 WSA_FLAG_REGISTERED_IO 로 만들어야 한다 — 일반 소켓은
	// RIORequestQueue 생성 자체가 WSAEOPNOTSUPP 로 실패한다. 대칭인 b(수신측)는 일반 소켓으로 충분.
	const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	ASSERT_NE(listener, INVALID_SOCKET);
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
	address.sin_port = 0;
	int_t addressLength = static_cast<int_t>(sizeof(address));
	ASSERT_EQ(::bind(listener, reinterpret_cast<sockaddr*>(&address), addressLength), 0);
	ASSERT_EQ(::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressLength), 0);
	ASSERT_EQ(::listen(listener, 1), 0);

	const SOCKET a = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_REGISTERED_IO);
	ASSERT_NE(a, INVALID_SOCKET);
	ASSERT_EQ(::connect(a, reinterpret_cast<sockaddr*>(&address), addressLength), 0);
	const SOCKET b = ::accept(listener, nullptr, nullptr);
	ASSERT_NE(b, INVALID_SOCKET);
	::closesocket(listener);

	int_t sendTag = 0;
	Request send{ .requestKind = RequestKind::SEND_ZERO_COPY, .userData = &sendTag, .handle = static_cast<ulonglong_t>(a), .buffer = region, .length = length, .bufferId = handle.value };
	engine.Submit(send);
	EXPECT_EQ(WaitFor(engine, &sendTag), static_cast<longlong_t>(length));

	char receiveBuffer[64]{};
	int_t receiveTag = 0;
	Request receive{ .requestKind = RequestKind::RECEIVE, .userData = &receiveTag, .handle = static_cast<ulonglong_t>(b), .buffer = receiveBuffer, .length = length };
	engine.Submit(receive);
	EXPECT_EQ(WaitFor(engine, &receiveTag), static_cast<longlong_t>(length));
	EXPECT_EQ(std::memcmp(receiveBuffer, region, length), 0);

	provider->UnregisterBuffer(handle);
	::closesocket(a);
	::closesocket(b);
}

// ── Capability 매트릭스 (스펙 2.2) ──
TEST(IoEngineTest, SupportsMatrix)
{
	const IocpEngine engine;
	EXPECT_TRUE(engine.Supports(Capability::SEND_FILE_ZERO_COPY));
	EXPECT_TRUE(engine.Supports(Capability::SEND_MEM_ZERO_COPY));
	EXPECT_FALSE(engine.Supports(Capability::RECEIVE_OVERHEAD_REDUCED)); // ReadFixed/WriteFixed 는 일반 파일 I/O 폴백(RIO 는 소켓 전용)
	EXPECT_FALSE(engine.Supports(Capability::RECEIVE_TRUE_ZERO_COPY));
}

// ── Wake 가 완료 없이도 대기를 즉시 해제 ──
TEST(IoEngineTest, WakeUnblocksWait)
{
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());

	engine.Wake();

	const auto start = std::chrono::steady_clock::now();
	Completion completions[4];
	const int_t count = engine.WaitCompletions(completions, 4, std::chrono::seconds(5));
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

	EXPECT_EQ(count, 0);      // Wake 는 완료를 만들지 않는다(대기만 해제)
	EXPECT_LT(elapsed, 1000); // 5s 타임아웃까지 블록하지 않고 즉시 풀려야 한다
}

#elif defined(IS_POSIX)

#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include "Io/Engine/Epoll/EpollEngine.h"
#include "Io/Engine/IoUring/IoUringEngine.h"

using namespace ne;using namespace ne::io;namespace
{
	longlong_t WaitFor(IEngine& _engine, void_t* _tag, const std::chrono::milliseconds _timeout = std::chrono::seconds(5))
	{
		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			Completion completions[8];
			const int_t count = _engine.WaitCompletions(completions, 8, std::chrono::milliseconds(50));
			for (int_t i = 0; i < count; ++i) if (completions[i].userData == _tag) return completions[i].result;
		}
		return -1;
	}

	// 임시 파일 Write → Read 왕복 (엔진 무관 — Submit/WaitCompletions 계약만 사용).
	void_t RunFileRoundTrip(IEngine& _engine)
	{
		const lpcstr_t path = "test_engine_posix_file.bin";
		const int_t fd = ::open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
		ASSERT_GE(fd, 0);

		const char payload[] = "posix-completion-model";
		const std::size_t length = sizeof(payload) - 1;

		int_t writeTag = 0;
		Request write{ .requestKind = RequestKind::WRITE, .userData = &writeTag, .handle = static_cast<ulonglong_t>(fd), .buffer = const_cast<char*>(payload), .length = length, .offset = 0 };
		_engine.Submit(write);
		EXPECT_EQ(WaitFor(_engine, &writeTag), static_cast<longlong_t>(length));

		char buffer[64]{};
		int_t readTag = 0;
		Request read{ .requestKind = RequestKind::READ, .userData = &readTag, .handle = static_cast<ulonglong_t>(fd), .buffer = buffer, .length = length, .offset = 0 };
		_engine.Submit(read);
		EXPECT_EQ(WaitFor(_engine, &readTag), static_cast<longlong_t>(length));
		EXPECT_EQ(std::memcmp(buffer, payload, length), 0);

		::close(fd);
		::unlink(path);
	}

	// socketpair Send → Receive 왕복.
	void_t RunSocketRoundTrip(IEngine& _engine)
	{
		int_t pair[2] = { -1, -1 };
		ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
		(void_t)::fcntl(pair[0], F_SETFL, O_NONBLOCK);
		(void_t)::fcntl(pair[1], F_SETFL, O_NONBLOCK);

		const char payload[] = "posix-socket-roundtrip";
		const std::size_t length = sizeof(payload) - 1;

		int_t sendTag = 0;
		Request send{ .requestKind = RequestKind::SEND, .userData = &sendTag, .handle = static_cast<ulonglong_t>(pair[0]), .buffer = const_cast<char*>(payload), .length = length };
		_engine.Submit(send);
		EXPECT_EQ(WaitFor(_engine, &sendTag), static_cast<longlong_t>(length));

		char buffer[64]{};
		int_t receiveTag = 0;
		Request receive{ .requestKind = RequestKind::RECEIVE, .userData = &receiveTag, .handle = static_cast<ulonglong_t>(pair[1]), .buffer = buffer, .length = length };
		_engine.Submit(receive);
		EXPECT_EQ(WaitFor(_engine, &receiveTag), static_cast<longlong_t>(length));
		EXPECT_EQ(std::memcmp(buffer, payload, length), 0);

		::close(pair[0]);
		::close(pair[1]);
	}
}TEST(EpollEngineTest, FileRoundTrip)
{
	EpollEngine engine;
	ASSERT_TRUE(engine.IsValid());
	RunFileRoundTrip(engine);
}TEST(EpollEngineTest, SocketRoundTrip)
{
	EpollEngine engine;
	ASSERT_TRUE(engine.IsValid());
	RunSocketRoundTrip(engine);
}TEST(EpollEngineTest, Supports)
{
	const EpollEngine engine;
	EXPECT_TRUE(engine.Supports(Capability::SEND_FILE_ZERO_COPY));
	EXPECT_TRUE(engine.Supports(Capability::SEND_MEM_ZERO_COPY));      // MSG_ZEROCOPY
	EXPECT_FALSE(engine.Supports(Capability::RECEIVE_OVERHEAD_REDUCED)); // 등록 버퍼 없음(plain epoll)
	EXPECT_FALSE(engine.Supports(Capability::RECEIVE_TRUE_ZERO_COPY));
}

// io_uring 은 커널(<5.1)/컨테이너 seccomp 로 막힐 수 있다 — 그 경우 SKIP(EpollEngine 폴백이 담당).
// 전체 io_uring 런타임 검증은 `docker run --security-opt seccomp=unconfined` 로.
TEST(IoUringEngineTest, FileRoundTrip)
{
	IoUringEngine engine;
	if (!engine.IsValid())
		GTEST_SKIP() << "io_uring unavailable";
	RunFileRoundTrip(engine);
}TEST(IoUringEngineTest, SocketRoundTrip)
{
	IoUringEngine engine;
	if (!engine.IsValid())
		GTEST_SKIP() << "io_uring unavailable";
	RunSocketRoundTrip(engine);
}

#endif // platform
