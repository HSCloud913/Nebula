// WsaPollEngine 은 IocpEngine 생성이 실패하는 극히 예외적인 경우에만 EngineFactory 가 내부적으로
// 선택하는 폴백이지만, 엔진 자체의 정확성은 test_engine.cpp 의 IocpEngine 케이스와 동일한 항목으로
// 직접 검증한다(Windows 전용 — Reactor 는 여기서만 인스턴스화, 공개 API 로는 노출되지 않는다).

#include <gtest/gtest.h>
#include "Base/Type.h"

#if defined(_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <chrono>
#include <cstring>
#include "Io/Engine/WsaPoll/WsaPollEngine.h"

using namespace ne;
using namespace ne::io;

namespace
{
	longlong_t WaitFor(WsaPollEngine& _engine, void_t* _tag, const std::chrono::milliseconds _timeout = std::chrono::seconds(5))
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

// ── 파일 Write → Read 왕복 (Perform() 내부 동기 완료 경로) ──
TEST(WsaPollEngineTest, FileWriteThenReadRoundTrip)
{
	// WsaPollEngine 생성자가 Wake() 용 소켓 쌍을 무조건 만들기 때문에, 이 테스트가 파일 I/O만
	// 다루더라도 Winsock 초기화가 먼저 되어 있어야 한다.
	const WsaScope wsa;
	WsaPollEngine engine;
	ASSERT_TRUE(engine.IsValid());

	const lpcstr_t path = "test_wsapoll_file.bin";
	const HANDLE file = ::CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_OVERLAPPED, nullptr);
	ASSERT_NE(file, INVALID_HANDLE_VALUE);

	const char payload[] = "wsapoll-completion-model";
	const std::size_t length = sizeof(payload) - 1;

	int_t writeTag = 0;
	Request write{ .op = OpCode::WRITE, .userData = &writeTag, .handle = reinterpret_cast<ulonglong_t>(file), .buffer = const_cast<lpstr_t>(payload), .length = length, .offset = 0 };
	engine.Submit(write);
	EXPECT_EQ(WaitFor(engine, &writeTag), static_cast<longlong_t>(length));

	char readBuffer[64]{};
	int_t readTag = 0;
	Request read{ .op = OpCode::READ, .userData = &readTag, .handle = reinterpret_cast<ulonglong_t>(file), .buffer = readBuffer, .length = length, .offset = 0 };
	engine.Submit(read);
	EXPECT_EQ(WaitFor(engine, &readTag), static_cast<longlong_t>(length));
	EXPECT_EQ(std::memcmp(readBuffer, payload, length), 0);

	::CloseHandle(file);
	::DeleteFileA(path);
}

// ── 소켓 Send → Receive 왕복 ──
TEST(WsaPollEngineTest, SocketSendThenReceiveRoundTrip)
{
	const WsaScope wsa;
	WsaPollEngine engine;
	ASSERT_TRUE(engine.IsValid());

	SOCKET a = INVALID_SOCKET;
	SOCKET b = INVALID_SOCKET;
	ASSERT_TRUE(MakeConnectedPair(a, b));

	const char payload[] = "wsapoll-socket-roundtrip";
	const std::size_t length = sizeof(payload) - 1;

	int_t sendTag = 0;
	Request send{ .op = OpCode::SEND, .userData = &sendTag, .handle = static_cast<ulonglong_t>(a), .buffer = const_cast<lpstr_t>(payload), .length = length };
	engine.Submit(send);
	EXPECT_EQ(WaitFor(engine, &sendTag), static_cast<longlong_t>(length));

	char receiveBuffer[64]{};
	int_t receiveTag = 0;
	Request receive{ .op = OpCode::RECEIVE, .userData = &receiveTag, .handle = static_cast<ulonglong_t>(b), .buffer = receiveBuffer, .length = length };
	engine.Submit(receive);
	EXPECT_EQ(WaitFor(engine, &receiveTag), static_cast<longlong_t>(length));
	EXPECT_EQ(std::memcmp(receiveBuffer, payload, length), 0);

	::closesocket(a);
	::closesocket(b);
}

// ── SendFile(TransmitFile) — 파일 → 소켓 zero-copy 전송(reactor 폴백에서도 RIO 없이 가능) ──
TEST(WsaPollEngineTest, SendFileZeroCopyRoundTrip)
{
	const WsaScope wsa;
	WsaPollEngine engine;
	ASSERT_TRUE(engine.IsValid());

	const lpcstr_t path = "test_wsapoll_sendfile.bin";
	const char payload[] = "wsapoll-sendfile-zerocopy";
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
	Request sendFile{ .op = OpCode::SEND_FILE, .userData = &sendFileTag, .handle = static_cast<ulonglong_t>(a), .length = length, .offset = 0, .auxHandle = reinterpret_cast<ulonglong_t>(sourceFile) };
	engine.Submit(sendFile);
	EXPECT_EQ(WaitFor(engine, &sendFileTag), static_cast<longlong_t>(length));

	char receiveBuffer[64]{};
	int_t receiveTag = 0;
	Request receive{ .op = OpCode::RECEIVE, .userData = &receiveTag, .handle = static_cast<ulonglong_t>(b), .buffer = receiveBuffer, .length = length };
	engine.Submit(receive);
	EXPECT_EQ(WaitFor(engine, &receiveTag), static_cast<longlong_t>(length));
	EXPECT_EQ(std::memcmp(receiveBuffer, payload, length), 0);

	::CloseHandle(sourceFile);
	::closesocket(a);
	::closesocket(b);
	::DeleteFileA(path);
}

// ── Accept/Connect 왕복 ──
TEST(WsaPollEngineTest, AcceptConnectRoundTrip)
{
	const WsaScope wsa;
	WsaPollEngine engine;
	ASSERT_TRUE(engine.IsValid());

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
	// Perform() 의 OpCode::ACCEPT 는 그대로 ::accept() 를 호출한다 — listener 가 블로킹 상태면
	// 연결이 들어올 때까지 Submit() 안에서 동기 블록되어(테스트 스레드가 아직 Connect 도
	// 제출하지 못한 시점) 데드락에 빠진다. epoll/io_uring 도 동일 계약이라 논블로킹 소켓
	// 사용은 상위(Socket/AsyncListener) 계층의 책임 — 엔진 직접 테스트에서도 재현해야 한다.
	u_long listenerNonBlocking = 1;
	::ioctlsocket(listener, FIONBIO, &listenerNonBlocking);

	const SOCKET client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	ASSERT_NE(client, INVALID_SOCKET);
	u_long nonBlocking = 1;
	::ioctlsocket(client, FIONBIO, &nonBlocking);

	int_t acceptTag = 0;
	Request accept{ .op = OpCode::ACCEPT, .userData = &acceptTag, .handle = static_cast<ulonglong_t>(listener) };
	engine.Submit(accept);

	int_t connectTag = 0;
	Request connect{ .op = OpCode::CONNECT, .userData = &connectTag, .handle = static_cast<ulonglong_t>(client), .address = &address, .addressLength = addressLength };
	engine.Submit(connect);

	// accept/connect 는 동시에 제출돼 같은 WaitCompletions 배치에서 함께 완료될 수 있다 —
	// WaitFor(단일 태그) 를 두 번 순차 호출하면 첫 호출이 자기 태그가 아닌 완료를 버려서
	// 두 번째 호출이 그 결과를 영영 못 보게 된다. 두 태그를 한 루프에서 같이 기다린다.
	longlong_t acceptResult = -1;
	longlong_t connectResult = -1;
	bool_t hasAccept = false;
	bool_t hasConnect = false;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while ((!hasAccept || !hasConnect) && std::chrono::steady_clock::now() < deadline)
	{
		Completion completions[8];
		const int_t count = engine.WaitCompletions(completions, 8, std::chrono::milliseconds(50));
		for (int_t i = 0; i < count; ++i)
		{
			if (completions[i].userData == &acceptTag)
			{
				acceptResult = completions[i].result;
				hasAccept = true;
			}
			else if (completions[i].userData == &connectTag)
			{
				connectResult = completions[i].result;
				hasConnect = true;
			}
		}
	}
	EXPECT_GE(acceptResult, 0);
	EXPECT_EQ(connectResult, 0);

	if (acceptResult >= 0) ::closesocket(static_cast<SOCKET>(acceptResult));
	::closesocket(client);
	::closesocket(listener);
}

// ── Capability 매트릭스 — TransmitFile(SendFile) 만 RIO 없이도 가능, 나머지는 reactor 폴백에서 불가 ──
TEST(WsaPollEngineTest, SupportsMatrix)
{
	const WsaPollEngine engine;
	EXPECT_TRUE(engine.Supports(Capability::SEND_FILE_ZERO_COPY));
	EXPECT_FALSE(engine.Supports(Capability::SEND_MEM_ZERO_COPY));
	EXPECT_FALSE(engine.Supports(Capability::RECEIVE_OVERHEAD_REDUCED));
	EXPECT_FALSE(engine.Supports(Capability::RECEIVE_TRUE_ZERO_COPY));
}

// ── Wake 가 완료 없이도 대기를 즉시 해제 ──
TEST(WsaPollEngineTest, WakeUnblocksWait)
{
	// 앞선 FileWriteThenReadRoundTrip 과 동일한 이유(생성자가 무조건 Wake 소켓 쌍을 만듦)로
	// Winsock 초기화가 필요하다.
	const WsaScope wsa;
	WsaPollEngine engine;
	ASSERT_TRUE(engine.IsValid());

	engine.Wake();

	const auto start = std::chrono::steady_clock::now();
	Completion completions[4];
	const int_t count = engine.WaitCompletions(completions, 4, std::chrono::seconds(5));
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

	EXPECT_EQ(count, 0);      // Wake 는 완료를 만들지 않는다(대기만 해제)
	EXPECT_LT(elapsed, 1000); // 5s 타임아웃까지 블록하지 않고 즉시 풀려야 한다
}

#endif // _WIN32
