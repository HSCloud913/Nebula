//
// Created by hscloud on 26. 6. 30.
//

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include "Network/Socket/Socket.h"
#include "Network/Stream/Plain/PlainStream.h"
#include "Base/Coroutine/Task.h"
#include "Memory/Allocator/PoolAllocator.h"
#include "Buffer/BufferBlock.h"

#if defined(IS_POSIX)
#	include "Io/Engine/Epoll/EpollEngine.h"
#elif defined(_WIN32)
#	include "Io/Engine/Iocp/IocpEngine.h"
#endif

using namespace ne::network;

namespace
{
	// 비동기 Task 를 blocking 으로 구동 — DNS 조회/연결은 워커 스레드에서 완료되므로 여기서 대기.
	template <typename T>
	T RunSync(ne::Task<T> _task)
	{
		_task.Resume();
		while (!_task.IsReady()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
		return _task.await_resume();
	}

	ne::Result<Socket, ne::OsError> CreateTcp(const AddressFamily _family = AddressFamily::IPv4) { return Socket::Create(_family, SOCK_STREAM, IPPROTO_TCP); }

	ne::Result<Socket, ne::OsError> CreateUdp(const AddressFamily _family = AddressFamily::IPv4) { return Socket::Create(_family, SOCK_DGRAM, IPPROTO_UDP); }

	#if defined(IS_POSIX)
	using TestEngine = ne::io::EpollEngine;
	#elif defined(_WIN32)
	using TestEngine = ne::io::IocpEngine;
	#endif

	// Connect(host,port,engine) 은 완료 통지를 RunOnce() 스레드로만 전달하므로 직접 구동해야 한다.
	template <typename T>
	T RunSyncWithEngine(ne::io::IIoEngine& _engine, ne::Task<T> _task, std::chrono::milliseconds _timeout = std::chrono::seconds(5))
	{
		_task.Resume();

		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while (!_task.IsReady() && std::chrono::steady_clock::now() < deadline) _engine.RunOnce(10);

		// 미완료 Task 를 await_resume() 하면 빈 optional 역참조(UB)이고, 그대로 반환해
		// Task 를 소멸시키면 진행 중 I/O 가 참조하는 코루틴 프레임이 파괴돼 UAF 가 된다.
		// 둘 다 안전하지 않으므로 테스트를 즉시 실패시킨다.
		if (!_task.IsReady())
		{
			ADD_FAILURE() << "RunSyncWithEngine: task did not complete within timeout";
			std::abort();
		}

		return _task.await_resume();
	}

	// 로컬 리스너를 열고 실제로 배정된 포트를 반환한다(Socket 은 getsockname 을 노출하지 않으므로 직접 조회).
	ne::Result<uint16_t, ne::OsError> BindEphemeralListener(Socket& _listener)
	{
		if (auto r = _listener.SetReuseAddress(true); r.IsError()) return ne::Result<uint16_t, ne::OsError>::Error(std::move(r.Error()));
		if (auto r = RunSync(_listener.Bind("127.0.0.1", 0)); r.IsError()) return ne::Result<uint16_t, ne::OsError>::Error(std::move(r.Error()));
		if (auto r = _listener.Listen(); r.IsError()) return ne::Result<uint16_t, ne::OsError>::Error(std::move(r.Error()));

		sockaddr_in addr{};
		socklen_t addrLen = sizeof(addr);
		if (::getsockname(_listener.Handle(), reinterpret_cast<sockaddr*>(&addr), &addrLen) != 0) return ne::Result<uint16_t, ne::OsError>::Error(ne::OsError{ ne::LastOsError() });

		return ne::Result<uint16_t, ne::OsError>::Ok(::ntohs(addr.sin_port));
	}
}

TEST(SocketTest, CreateTcpSucceeds)
{
	auto result = CreateTcp();
	ASSERT_TRUE(result.IsOk());
	EXPECT_TRUE(result.Value().IsValid());
}

TEST(SocketTest, CreateUdpSucceeds)
{
	auto result = CreateUdp();
	ASSERT_TRUE(result.IsOk());
	EXPECT_TRUE(result.Value().IsValid());
}

#if defined(_WIN32)
TEST(SocketTest, CreateRegisteredIoSucceeds)
{
	// RIO 소켓은 WSASocketW + WSA_FLAG_REGISTERED_IO 로 생성돼야 한다(RIO provider 전제).
	auto result = Socket::Create(AddressFamily::IPv4, SOCK_STREAM, IPPROTO_TCP, SocketCreateFlags::RegisteredIo);
	ASSERT_TRUE(result.IsOk());
	EXPECT_TRUE(result.Value().IsValid());
}
#endif

TEST(SocketTest, MoveInvalidatesSource)
{
	auto result = CreateTcp();
	ASSERT_TRUE(result.IsOk());

	Socket a = std::move(result.Value());
	EXPECT_TRUE(a.IsValid());

	Socket b = std::move(a);
	EXPECT_FALSE(a.IsValid());
	EXPECT_TRUE(b.IsValid());
}

TEST(SocketTest, SetReuseAddrSucceeds)
{
	auto result = CreateTcp();
	ASSERT_TRUE(result.IsOk());

	auto optResult = result.Value().SetReuseAddress(true);
	EXPECT_TRUE(optResult.IsOk());
}

TEST(SocketTest, BindFailsOnInvalidAddress)
{
	auto result = CreateTcp();
	ASSERT_TRUE(result.IsOk());

	// 존재하지 않는 주소에 바인드하면 실패해야 함.
	auto bindResult = RunSync(result.Value().Bind("999.999.999.999", 0));
	EXPECT_TRUE(bindResult.IsError());
}

TEST(SocketTest, ResolveFamilyDetectsLiterals)
{
	auto v4 = RunSync(Socket::ResolveFamily("127.0.0.1"));
	ASSERT_TRUE(v4.IsOk());
	EXPECT_EQ(v4.Value(), AddressFamily::IPv4);

	auto v6 = RunSync(Socket::ResolveFamily("::1"));
	ASSERT_TRUE(v6.IsOk());
	EXPECT_EQ(v6.Value(), AddressFamily::IPv6);
}

TEST(SocketTest, CreateIpv6Succeeds)
{
	auto result = CreateTcp(AddressFamily::IPv6);
	ASSERT_TRUE(result.IsOk());
	EXPECT_TRUE(result.Value().IsValid());
	EXPECT_EQ(result.Value().Family(), AddressFamily::IPv6);
}

TEST(SocketTest, Ipv6BindAndAcceptRoundTrip)
{
	auto serverResult = CreateTcp(AddressFamily::IPv6);
	ASSERT_TRUE(serverResult.IsOk());
	Socket server = std::move(serverResult.Value());

	ASSERT_TRUE(server.SetReuseAddress(true).IsOk());
	ASSERT_TRUE(RunSync(server.Bind("::1", 0)).IsOk());
	ASSERT_TRUE(server.Listen().IsOk());
}

// ── 비동기 Connect(host,port,engine) ────────────────────────────────────────
// TCP 핸드셰이크는 리스너가 LISTEN 상태이기만 하면 accept() 호출 없이도 백로그에서
// 완료되므로, 여기선 accept 를 부르지 않고 connect 결과만 확인한다.
TEST(SocketTest, ConnectAsyncSucceedsToLocalListener)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());

	auto serverResult = CreateTcp();
	ASSERT_TRUE(serverResult.IsOk());
	Socket server = std::move(serverResult.Value());

	auto portResult = BindEphemeralListener(server);
	ASSERT_TRUE(portResult.IsOk()) << portResult.Error().What();

	auto clientResult = CreateTcp();
	ASSERT_TRUE(clientResult.IsOk());
	Socket client = std::move(clientResult.Value());

	auto connectResult = RunSyncWithEngine(engine, client.Connect("127.0.0.1", portResult.Value(), engine));
	EXPECT_TRUE(connectResult.IsOk()) << connectResult.Error().What();
}

// 핵심 회귀 테스트: 아무도 듣지 않는 포트로의 connect 는 blocking OS 타임아웃(수십 초)까지
// 기다리지 않고 즉시(수백 ms 이내) ECONNREFUSED 로 실패해야 한다 — non-blocking connect +
// SO_ERROR 확인이 실제로 동작하는지 검증.
TEST(SocketTest, ConnectAsyncFailsQuicklyOnRefusedPort)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());

	// 잠깐 리스너를 열어 실제로 비어있던 포트 번호를 얻은 뒤 바로 닫는다 — 그 직후엔
	// 아무도 듣고 있지 않으므로 connect 가 ECONNREFUSED 로 즉시 실패한다.
	uint16_t port = 0;
	{
		auto serverResult = CreateTcp();
		ASSERT_TRUE(serverResult.IsOk());
		Socket server = std::move(serverResult.Value());

		auto portResult = BindEphemeralListener(server);
		ASSERT_TRUE(portResult.IsOk()) << portResult.Error().What();
		port = portResult.Value();
	}

	auto clientResult = CreateTcp();
	ASSERT_TRUE(clientResult.IsOk());
	Socket client = std::move(clientResult.Value());

	const auto start = std::chrono::steady_clock::now();
	auto connectResult = RunSyncWithEngine(engine, client.Connect("127.0.0.1", port, engine), std::chrono::seconds(5));
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

	EXPECT_TRUE(connectResult.IsError());
	EXPECT_LT(elapsed, 2000) << "refused connect must fail quickly, not block for the OS connect timeout";
}

#if defined(_WIN32)
// ── PlainStream::ReceiveFile — 소켓 바이트를 파일로 드레인 (A: 소켓→파일) ──
TEST(SocketTest, PlainStreamReceiveFileDrainsSocketToFile)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());

	// 리스너 + 클라이언트 연결
	auto serverResult = CreateTcp();
	ASSERT_TRUE(serverResult.IsOk());
	Socket server = std::move(serverResult.Value());
	auto portResult = BindEphemeralListener(server);
	ASSERT_TRUE(portResult.IsOk()) << portResult.Error().What();

	auto clientResult = CreateTcp();
	ASSERT_TRUE(clientResult.IsOk());
	Socket client = std::move(clientResult.Value());
	auto connectResult = RunSyncWithEngine(engine, client.Connect("127.0.0.1", portResult.Value(), engine));
	ASSERT_TRUE(connectResult.IsOk()) << connectResult.Error().What();

	// 서버 accept → PlainStream 수신자
	auto acceptRes = server.Accept();
	ASSERT_TRUE(acceptRes.IsOk()) << acceptRes.Error().What();
	auto psRes = PlainStream::Accept(std::move(acceptRes.Value()), engine);
	ASSERT_TRUE(psRes.IsOk()) << psRes.Error().What();
	PlainStream receiver = std::move(psRes.Value());

	// 클라이언트가 정확히 payload 만큼 전송
	const char payload[] = "hello-receive-file-zero-copy";
	const int payloadLen = static_cast<int>(sizeof(payload) - 1);
	ASSERT_EQ(::send(client.Handle(), payload, payloadLen, 0), payloadLen);

	// 수신 파일(동기 핸들 — Windows ReceiveFile 전제)
	const char* path = "test_receivefile.bin";
	HANDLE file = ::CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	ASSERT_NE(file, INVALID_HANDLE_VALUE);

	auto rfRes = RunSyncWithEngine(engine, receiver.ReceiveFile(file, 0, static_cast<std::size_t>(payloadLen)));
	ASSERT_TRUE(rfRes.IsOk()) << rfRes.Error().What();
	EXPECT_EQ(rfRes.Value(), static_cast<std::size_t>(payloadLen));

	// 파일 내용 검증
	::SetFilePointer(file, 0, nullptr, FILE_BEGIN);
	char readBuf[64]{};
	DWORD read = 0;
	ASSERT_TRUE(::ReadFile(file, readBuf, payloadLen, &read, nullptr));
	EXPECT_EQ(static_cast<int>(read), payloadLen);
	EXPECT_EQ(std::memcmp(readBuf, payload, payloadLen), 0);

	::CloseHandle(file);
	::DeleteFileA(path);
	::closesocket(client.Handle());
}

// ── PlainStream::SendRegistered — RIO 등록버퍼 송신 왕복 (fast path → 완료 demux 발화) ──
TEST(SocketTest, PlainStreamSendRegisteredRioRoundTrip)
{
	TestEngine engine; // IocpEngine
	ASSERT_TRUE(engine.IsValid());
	ASSERT_TRUE(ne::io::HasCapability(engine.Capabilities(), ne::io::IoCapability::RegisteredIo));

	// 리스너
	auto serverResult = CreateTcp();
	ASSERT_TRUE(serverResult.IsOk());
	Socket server = std::move(serverResult.Value());
	auto portResult = BindEphemeralListener(server);
	ASSERT_TRUE(portResult.IsOk()) << portResult.Error().What();

	// 클라이언트 — RIO 소켓(WSA_FLAG_REGISTERED_IO)으로 생성 후 blocking connect(reactor 미사용,
	// RIO 데이터경로와 분리). RIO 는 소켓별 RIO_RQ 로 제출하므로 reactor watch 와 섞지 않는다.
	auto clientResult = Socket::Create(AddressFamily::IPv4, SOCK_STREAM, IPPROTO_TCP, SocketCreateFlags::RegisteredIo);
	ASSERT_TRUE(clientResult.IsOk()) << clientResult.Error().What();
	Socket client = std::move(clientResult.Value());
	auto connectResult = RunSync(client.Connect("127.0.0.1", portResult.Value()));
	ASSERT_TRUE(connectResult.IsOk()) << connectResult.Error().What();

	auto acceptRes = server.Accept();
	ASSERT_TRUE(acceptRes.IsOk()) << acceptRes.Error().What();
	Socket accepted = std::move(acceptRes.Value());

	// 클라이언트를 PlainStream 으로 감싼다(블로킹 모드 변경 안 함 — SendRegistered 는 RIO 경로).
	auto psRes = PlainStream::Create(std::move(client), engine);
	ASSERT_TRUE(psRes.IsOk()) << psRes.Error().What();
	PlainStream sender = std::move(psRes.Value());

	// BufferBlock 등록 → RegisteredBuffer 구성 → payload 채우기
	ne::memory::PoolAllocator pool(512, 4);
	auto blockRes = ne::io::BufferBlock::Acquire(pool, 256);
	ASSERT_TRUE(blockRes.IsOk());
	ne::io::BufferBlock* block = blockRes.Value();

	auto* provider = engine.AsRegisteredBufferProvider();
	ASSERT_NE(provider, nullptr);
	auto regRes = provider->RegisterBuffer(block->Data());
	ASSERT_TRUE(regRes.IsOk()) << regRes.Error().What();

	const char payload[] = "hello-rio-registered-send";
	const std::size_t payloadLen = sizeof(payload) - 1;
	std::memcpy(block->Data().data(), payload, payloadLen);

	ne::io::RegisteredBuffer rb{ regRes.Value(), ne::io::BufferView{ block, block->Data().data(), payloadLen } };

	// RIOSend 제출 → 엔진 구동 → RIO 완료가 IOCP(RioKey)로 통지 → DrainRioCompletions 가 resume.
	auto sendRes = RunSyncWithEngine(engine, sender.SendRegistered(rb));
	ASSERT_TRUE(sendRes.IsOk()) << sendRes.Error().What();
	EXPECT_EQ(sendRes.Value(), payloadLen);

	// 상대(accept)가 plain recv 로 정확히 payload 를 받는다.
	char recvBuf[64]{};
	const int got = ::recv(accepted.Handle(), recvBuf, static_cast<int>(payloadLen), 0);
	EXPECT_EQ(got, static_cast<int>(payloadLen));
	EXPECT_EQ(std::memcmp(recvBuf, payload, payloadLen), 0);

	provider->UnregisterBuffer(regRes.Value());
	block->Release();
	(void)sender.Close();
	::closesocket(accepted.Handle());
}

// ── PlainStream::Sendv — Proactor 벡터(scatter-gather) 단일 WSASend 왕복 ──
TEST(SocketTest, PlainStreamSendvProactorVectored)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());

	auto serverResult = CreateTcp();
	ASSERT_TRUE(serverResult.IsOk());
	Socket server = std::move(serverResult.Value());
	auto portResult = BindEphemeralListener(server);
	ASSERT_TRUE(portResult.IsOk()) << portResult.Error().What();

	// blocking connect — reactor 등록을 피해 소켓을 Proactor(ProactorKey) 로만 쓴다.
	auto clientResult = CreateTcp();
	ASSERT_TRUE(clientResult.IsOk());
	Socket client = std::move(clientResult.Value());
	auto connectResult = RunSync(client.Connect("127.0.0.1", portResult.Value()));
	ASSERT_TRUE(connectResult.IsOk()) << connectResult.Error().What();

	auto acceptRes = server.Accept();
	ASSERT_TRUE(acceptRes.IsOk()) << acceptRes.Error().What();
	Socket accepted = std::move(acceptRes.Value());

	auto psRes = PlainStream::Create(std::move(client), engine, nullptr, IoMode::Proactor);
	ASSERT_TRUE(psRes.IsOk()) << psRes.Error().What();
	PlainStream sender = std::move(psRes.Value());

	// 2개 세그먼트 — 유저공간 concat 없이 단일 WSASend 로 순서대로 전송돼야 한다.
	ne::memory::PoolAllocator pool(256, 4);
	auto b1 = ne::io::BufferBlock::Acquire(pool, 64);
	auto b2 = ne::io::BufferBlock::Acquire(pool, 64);
	ASSERT_TRUE(b1.IsOk());
	ASSERT_TRUE(b2.IsOk());
	ne::io::BufferBlock* blk1 = b1.Value();
	ne::io::BufferBlock* blk2 = b2.Value();

	const char part1[] = "hello-";
	const char part2[] = "vectored-world";
	const std::size_t len1 = sizeof(part1) - 1;
	const std::size_t len2 = sizeof(part2) - 1;
	std::memcpy(blk1->Data().data(), part1, len1);
	std::memcpy(blk2->Data().data(), part2, len2);

	ne::io::BufferChain chain;
	chain.Append(ne::io::BufferView{ blk1, blk1->Data().data(), len1 });
	chain.Append(ne::io::BufferView{ blk2, blk2->Data().data(), len2 });

	const std::size_t totalLen = len1 + len2;
	auto sendRes = RunSyncWithEngine(engine, sender.Sendv(chain));
	ASSERT_TRUE(sendRes.IsOk()) << sendRes.Error().What();
	EXPECT_EQ(sendRes.Value(), totalLen);

	char recvBuf[64]{};
	std::size_t got = 0;
	while (got < totalLen)
	{
		const int n = ::recv(accepted.Handle(), recvBuf + got, static_cast<int>(totalLen - got), 0);
		ASSERT_GT(n, 0);
		got += static_cast<std::size_t>(n);
	}
	EXPECT_EQ(std::memcmp(recvBuf, "hello-vectored-world", totalLen), 0);

	blk1->Release();
	blk2->Release();
	(void)sender.Close();
	::closesocket(accepted.Handle());
}
#endif
