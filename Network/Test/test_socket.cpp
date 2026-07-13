//
// Created by hscloud on 26. 6. 30.
//
// PlainStream(ne::io::Socket 위의 async-only IStream 어댑터) 테스트. Network 모듈이 실제로
// 빌드/링크하는 건 Dns.cpp + PlainStream.cpp 뿐이라(Network/CMakeLists.txt 참고 — Socket/TLS/SSH/
// Http/Ftp 는 전부 주석 처리돼 미빌드), 여기서는 그 표면만 검증한다.

#include <gtest/gtest.h>

#if defined(_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <cstring>
#include <span>
#include <utility>
#include <vector>
#include "Base/Coroutine/Task.h"
#include "Io/Buffer/RegisteredBuffer.h"
#include "Io/Context/Context.h"
#include "Io/Engine/Iocp/IocpEngine.h"
#include "Io/File/File.h"
#include "Io/Socket/Socket.h"
#include "Network/Stream/Plain/PlainStream.h"

using namespace ne;
using namespace ne::io;
using ne::network::PlainStream;

namespace
{
	using TestEngine = IocpEngine;

	template <typename T>
	T Drive(Context& _context, ne::Task<T>& _task, const std::chrono::milliseconds _timeout = std::chrono::seconds(5))
	{
		_task.Resume();
		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		while (!_task.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 10 });
		return _task.await_resume();
	}

	// 로컬 리스너를 열고 실제로 배정된 포트를 반환한다(io::Socket 은 getsockname 을 노출하지
	// 않으므로 raw handle 로 직접 조회).
	IoResult<uint16_t> BindEphemeralListener(Socket& _listener)
	{
		using R = IoResult<uint16_t>;
		if (auto r = _listener.Bind("127.0.0.1", 0); r.IsError()) return R::Error(std::move(r.Error()));
		if (auto r = _listener.Listen(); r.IsError()) return R::Error(std::move(r.Error()));

		sockaddr_in addr{};
		socklen_t addrLen = sizeof(addr);
		if (::getsockname(_listener.Handle(), reinterpret_cast<sockaddr*>(&addr), &addrLen) != 0) return R::Error(IoError{ IoErrorKind::OS_FAILURE, "getsockname failed" });

		return R::Ok(::ntohs(addr.sin_port));
	}

	// Accept(AcceptEx)와 Connect(ConnectEx)는 서로 의존한다 — 둘 다 제출한 뒤 함께 구동해야
	// 완료된다(한쪽만 돌리면 교착).
	IoResult<std::pair<Socket, Socket>> ConnectPair(Context& _context, Socket& _listener, const uint16_t _port, const bool_t _clientRegisteredIo = false)
	{
		using R = IoResult<std::pair<Socket, Socket>>;

		auto clientResult = Socket::Create(_context, AF_INET, SOCK_STREAM, IPPROTO_TCP, _clientRegisteredIo);
		if (clientResult.IsError()) return R::Error(std::move(clientResult.Error()));
		Socket client = std::move(clientResult.Value());

		auto acceptTask = _listener.Accept();
		auto connectTask = client.Connect("127.0.0.1", _port);
		acceptTask.Resume();
		connectTask.Resume();

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while ((!acceptTask.IsReady() || !connectTask.IsReady()) && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 10 });
		if (!acceptTask.IsReady() || !connectTask.IsReady()) return R::Error(IoError{ IoErrorKind::OS_FAILURE, "accept/connect did not complete in time" });

		auto acceptResult = acceptTask.await_resume();
		auto connectResult = connectTask.await_resume();
		if (acceptResult.IsError()) return R::Error(std::move(acceptResult.Error()));
		if (connectResult.IsError()) return R::Error(std::move(connectResult.Error()));

		return R::Ok(std::make_pair(std::move(acceptResult.Value()), std::move(client)));
	}

	struct StreamPair
	{
		PlainStream server; // listener 가 accept 한 쪽
		PlainStream client; // 능동 connect 한 쪽
	};

	// 리스너를 새로 열고, accept/connect 로 연결한 뒤 양쪽 다 PlainStream 으로 감싼다.
	IoResult<StreamPair> MakeConnectedStreamPair(Context& _context, const bool_t _clientRegisteredIo = false)
	{
		using R = IoResult<StreamPair>;

		auto listenerResult = Socket::Create(_context, AF_INET);
		if (listenerResult.IsError()) return R::Error(std::move(listenerResult.Error()));
		Socket listener = std::move(listenerResult.Value());

		auto portResult = BindEphemeralListener(listener);
		if (portResult.IsError()) return R::Error(std::move(portResult.Error()));

		auto pairResult = ConnectPair(_context, listener, portResult.Value(), _clientRegisteredIo);
		if (pairResult.IsError()) return R::Error(std::move(pairResult.Error()));
		auto [accepted, client] = std::move(pairResult.Value());

		auto serverStream = PlainStream::Create(std::move(accepted), _context);
		if (serverStream.IsError()) return R::Error(std::move(serverStream.Error()));
		auto clientStream = PlainStream::Create(std::move(client), _context);
		if (clientStream.IsError()) return R::Error(std::move(clientStream.Error()));

		return R::Ok(StreamPair{ std::move(serverStream.Value()), std::move(clientStream.Value()) });
	}
}

// ── PlainStream::Create — 유효하지 않은 소켓은 거부한다 ──
TEST(PlainStreamTest, CreateRejectsInvalidSocket)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	Socket invalid = std::move(Socket::Create(context, AF_INET).Value());
	ASSERT_TRUE(invalid.Close().IsOk()); // 닫아서 무효화

	auto result = PlainStream::Create(std::move(invalid), context);
	EXPECT_TRUE(result.IsError());
}

// ── Send/Receive 왕복 ──
TEST(PlainStreamTest, SendThenReceiveRoundTrip)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto pairResult = MakeConnectedStreamPair(context);
	ASSERT_TRUE(pairResult.IsOk()) << pairResult.Error().What();
	auto [server, client] = std::move(pairResult.Value());

	const char payload[] = "plainstream-send-receive";
	const std::size_t length = sizeof(payload) - 1;

	auto sendTask = client.Send(BufferView{ reinterpret_cast<byte_t*>(const_cast<char*>(payload)), length });
	auto sendResult = Drive(context, sendTask);
	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();
	EXPECT_EQ(sendResult.Value(), length);

	byte_t buffer[64]{};
	auto receiveTask = server.Receive(BufferView{ buffer, length });
	auto receiveResult = Drive(context, receiveTask);
	ASSERT_TRUE(receiveResult.IsOk()) << receiveResult.Error().What();
	EXPECT_EQ(receiveResult.Value(), length);
	EXPECT_EQ(std::memcmp(buffer, payload, length), 0);
}

// ── Sendv/Receivev — scatter/gather 벡터 왕복 ──
TEST(PlainStreamTest, SendvReceivevScatterGather)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto pairResult = MakeConnectedStreamPair(context);
	ASSERT_TRUE(pairResult.IsOk()) << pairResult.Error().What();
	auto [server, client] = std::move(pairResult.Value());

	char part1[] = "hello-";
	char part2[] = "vectored-world";
	const std::size_t len1 = sizeof(part1) - 1;
	const std::size_t len2 = sizeof(part2) - 1;
	const std::size_t totalLen = len1 + len2;

	BufferChain sendChain;
	sendChain.Append(BufferView{ reinterpret_cast<byte_t*>(part1), len1 });
	sendChain.Append(BufferView{ reinterpret_cast<byte_t*>(part2), len2 });

	auto sendTask = client.Sendv(sendChain);
	auto sendResult = Drive(context, sendTask);
	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();
	EXPECT_EQ(sendResult.Value(), totalLen);

	byte_t bufferA[6]{};
	byte_t bufferB[14]{};
	BufferChain receiveChain;
	receiveChain.Append(BufferView{ bufferA, sizeof(bufferA) });
	receiveChain.Append(BufferView{ bufferB, sizeof(bufferB) });

	auto receiveTask = server.Receivev(receiveChain);
	auto receiveResult = Drive(context, receiveTask);
	ASSERT_TRUE(receiveResult.IsOk()) << receiveResult.Error().What();
	EXPECT_EQ(receiveResult.Value(), totalLen);
	EXPECT_EQ(std::memcmp(bufferA, part1, sizeof(bufferA)), 0);
	EXPECT_EQ(std::memcmp(bufferB, part2, sizeof(bufferB)), 0);
}

// ── Shutdown: send 방향만 half-close. 상대는 남은 데이터를 읽은 뒤 EOF(Receive == 0)를 본다 ──
TEST(PlainStreamTest, ShutdownHalfClosesSendAndSignalsEof)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto pairResult = MakeConnectedStreamPair(context);
	ASSERT_TRUE(pairResult.IsOk()) << pairResult.Error().What();
	auto [server, client] = std::move(pairResult.Value());

	const char payload[] = "bye";
	const std::size_t length = sizeof(payload) - 1;

	auto sendTask = client.Send(BufferView{ reinterpret_cast<byte_t*>(const_cast<char*>(payload)), length });
	auto sendResult = Drive(context, sendTask);
	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();

	auto shutdownTask = client.Shutdown();
	auto shutdownResult = Drive(context, shutdownTask);
	ASSERT_TRUE(shutdownResult.IsOk()) << shutdownResult.Error().What();

	byte_t buffer[16]{};
	auto firstTask = server.Receive(BufferView{ buffer, sizeof(buffer) });
	auto firstResult = Drive(context, firstTask);
	ASSERT_TRUE(firstResult.IsOk()) << firstResult.Error().What();
	EXPECT_EQ(firstResult.Value(), length);

	auto eofTask = server.Receive(BufferView{ buffer, sizeof(buffer) });
	auto eofResult = Drive(context, eofTask);
	ASSERT_TRUE(eofResult.IsOk()) << eofResult.Error().What();
	EXPECT_EQ(eofResult.Value(), 0u);
}

// ── Close: 이후 IsOpen()==false 이고 모든 op 이 "closed" 값 에러로 즉시 실패한다 ──
TEST(PlainStreamTest, CloseInvalidatesStreamAndRejectsFurtherOps)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto pairResult = MakeConnectedStreamPair(context);
	ASSERT_TRUE(pairResult.IsOk()) << pairResult.Error().What();
	auto [server, client] = std::move(pairResult.Value());

	EXPECT_TRUE(client.IsOpen());
	ASSERT_TRUE(client.Close().IsOk());
	EXPECT_FALSE(client.IsOpen());

	byte_t buffer[8]{};
	auto sendTask = client.Send(BufferView{ buffer, sizeof(buffer) });
	auto sendResult = Drive(context, sendTask);
	EXPECT_TRUE(sendResult.IsError());
}

// ── PlainStream::Connect — 호스트 문자열(IP 리터럴) 해석 + 연결까지 한 번에 ──
TEST(PlainStreamTest, ConnectResolvesLiteralAndConnectsToLocalListener)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto listenerResult = Socket::Create(context, AF_INET);
	ASSERT_TRUE(listenerResult.IsOk()) << listenerResult.Error().What();
	Socket listener = std::move(listenerResult.Value());
	auto portResult = BindEphemeralListener(listener);
	ASSERT_TRUE(portResult.IsOk()) << portResult.Error().What();

	auto acceptTask = listener.Accept();
	auto connectTask = PlainStream::Connect("127.0.0.1", portResult.Value(), context);
	acceptTask.Resume();
	connectTask.Resume();

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while ((!acceptTask.IsReady() || !connectTask.IsReady()) && std::chrono::steady_clock::now() < deadline) (void_t)context.RunOnce(std::chrono::milliseconds{ 10 });
	ASSERT_TRUE(acceptTask.IsReady());
	ASSERT_TRUE(connectTask.IsReady());

	auto acceptResult = acceptTask.await_resume();
	auto connectResult = connectTask.await_resume();
	ASSERT_TRUE(acceptResult.IsOk()) << acceptResult.Error().What();
	ASSERT_TRUE(connectResult.IsOk()) << connectResult.Error().What();

	auto serverStream = PlainStream::Create(std::move(acceptResult.Value()), context);
	ASSERT_TRUE(serverStream.IsOk()) << serverStream.Error().What();
	PlainStream server = std::move(serverStream.Value());
	PlainStream client = std::move(connectResult.Value());

	const char payload[] = "dns-connect-roundtrip";
	const std::size_t length = sizeof(payload) - 1;
	auto sendTask = client.Send(BufferView{ reinterpret_cast<byte_t*>(const_cast<char*>(payload)), length });
	auto sendResult = Drive(context, sendTask);
	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();

	byte_t buffer[32]{};
	auto receiveTask = server.Receive(BufferView{ buffer, length });
	auto receiveResult = Drive(context, receiveTask);
	ASSERT_TRUE(receiveResult.IsOk()) << receiveResult.Error().What();
	EXPECT_EQ(receiveResult.Value(), length);
	EXPECT_EQ(std::memcmp(buffer, payload, length), 0);
}

// ── PlainStream::ReceiveFile — 소켓 바이트를 파일로 드레인 ──
TEST(PlainStreamTest, ReceiveFileDrainsSocketToFile)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto pairResult = MakeConnectedStreamPair(context);
	ASSERT_TRUE(pairResult.IsOk()) << pairResult.Error().What();
	auto [server, client] = std::move(pairResult.Value());

	const char payload[] = "hello-receive-file";
	const int payloadLen = static_cast<int>(sizeof(payload) - 1);
	auto sendTask = client.Send(BufferView{ reinterpret_cast<byte_t*>(const_cast<char*>(payload)), static_cast<std::size_t>(payloadLen) });
	auto sendResult = Drive(context, sendTask);
	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();

	const lpcstr_t path = "test_plainstream_receivefile.bin";
	::DeleteFileA(path); // 이전 실행 잔여물 제거(READ_WRITE 는 OPEN_ALWAYS 라 트렁케이트하지 않음)
	auto fileResult = File::Open(context, path, OpenMode::READ_WRITE);
	ASSERT_TRUE(fileResult.IsOk()) << fileResult.Error().What();
	File file = std::move(fileResult.Value());

	auto rfTask = server.ReceiveFile(file, 0, static_cast<std::size_t>(payloadLen));
	auto rfResult = Drive(context, rfTask);
	ASSERT_TRUE(rfResult.IsOk()) << rfResult.Error().What();
	EXPECT_EQ(rfResult.Value(), static_cast<std::size_t>(payloadLen));

	byte_t readBuf[64]{};
	auto readTask = file.Read(std::span<byte_t>{ readBuf, static_cast<std::size_t>(payloadLen) }, 0);
	auto readResult = Drive(context, readTask);
	ASSERT_TRUE(readResult.IsOk()) << readResult.Error().What();
	EXPECT_EQ(readResult.Value(), static_cast<std::size_t>(payloadLen));
	EXPECT_EQ(std::memcmp(readBuf, payload, payloadLen), 0);

	(void_t)file.Close();
	::DeleteFileA(path);
}

// ── PlainStream::SendFile — head(Sendv) + file(zero-copy TransmitFile) + tail(Sendv) 조합 ──
TEST(PlainStreamTest, SendFileCombinesHeadFileAndTail)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	auto pairResult = MakeConnectedStreamPair(context);
	ASSERT_TRUE(pairResult.IsOk()) << pairResult.Error().What();
	auto [server, client] = std::move(pairResult.Value());

	// 전송할 파일 본문 준비.
	const char body[] = "middle-file-body";
	const std::size_t bodyLen = sizeof(body) - 1;
	const lpcstr_t path = "test_plainstream_sendfile.bin";
	::DeleteFileA(path);
	auto fileResult = File::Open(context, path, OpenMode::READ_WRITE);
	ASSERT_TRUE(fileResult.IsOk()) << fileResult.Error().What();
	File file = std::move(fileResult.Value());

	auto writeTask = file.Write(std::span<const byte_t>{ reinterpret_cast<const byte_t*>(body), bodyLen }, 0);
	auto writeResult = Drive(context, writeTask);
	ASSERT_TRUE(writeResult.IsOk()) << writeResult.Error().What();

	char head[] = "HEAD-";
	char tail[] = "-TAIL";
	const std::size_t headLen = sizeof(head) - 1;
	const std::size_t tailLen = sizeof(tail) - 1;
	BufferChain headChain;
	headChain.Append(BufferView{ reinterpret_cast<byte_t*>(head), headLen });
	BufferChain tailChain;
	tailChain.Append(BufferView{ reinterpret_cast<byte_t*>(tail), tailLen });

	auto sendFileTask = client.SendFile(file.Handle(), 0, bodyLen, headChain, tailChain);
	auto sendFileResult = Drive(context, sendFileTask);
	ASSERT_TRUE(sendFileResult.IsOk()) << sendFileResult.Error().What();
	const std::size_t totalLen = headLen + bodyLen + tailLen;
	EXPECT_EQ(sendFileResult.Value(), totalLen);

	char recvBuf[64]{};
	std::size_t got = 0;
	while (got < totalLen)
	{
		auto receiveTask = server.Receive(BufferView{ reinterpret_cast<byte_t*>(recvBuf) + got, totalLen - got });
		auto receiveResult = Drive(context, receiveTask);
		ASSERT_TRUE(receiveResult.IsOk()) << receiveResult.Error().What();
		ASSERT_GT(receiveResult.Value(), 0u);
		got += receiveResult.Value();
	}
	EXPECT_EQ(std::memcmp(recvBuf, "HEAD-middle-file-body-TAIL", totalLen), 0);

	(void_t)file.Close();
	::DeleteFileA(path);
}

// ── PlainStream::SendRegistered — RIO 등록버퍼 fast path. 이 엔진이 RIO provider 를 안 갖고
// 있으면(RegisteredBuffer::Register 가 UNSUPPORTED) 해당 환경에서 검증할 수 없으므로 skip. ──
TEST(PlainStreamTest, SendRegisteredRioFastPathRoundTrip)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	const char payload[] = "hello-rio-registered-send";
	const std::size_t payloadLen = sizeof(payload) - 1;
	std::vector<byte_t> region(payloadLen);
	std::memcpy(region.data(), payload, payloadLen);

	auto regResult = RegisteredBuffer::Register(engine, std::span<byte_t>{ region });
	if (regResult.IsError() && regResult.Error().IsUnsupported()) GTEST_SKIP() << "engine has no registered buffer provider";
	ASSERT_TRUE(regResult.IsOk()) << regResult.Error().What();
	RegisteredBuffer rb = std::move(regResult.Value());

	auto listenerResult = Socket::Create(context, AF_INET);
	ASSERT_TRUE(listenerResult.IsOk()) << listenerResult.Error().What();
	Socket listener = std::move(listenerResult.Value());
	auto portResult = BindEphemeralListener(listener);
	ASSERT_TRUE(portResult.IsOk()) << portResult.Error().What();

	// 클라이언트만 RIO 소켓(WSA_FLAG_REGISTERED_IO)으로 생성 — SendZeroCopy(RIO) 는 송신측 소켓에서만 필요.
	auto pairResult = ConnectPair(context, listener, portResult.Value(), /*_clientRegisteredIo=*/true);
	ASSERT_TRUE(pairResult.IsOk()) << pairResult.Error().What();
	auto [accepted, client] = std::move(pairResult.Value());

	auto senderResult = PlainStream::Create(std::move(client), context);
	ASSERT_TRUE(senderResult.IsOk()) << senderResult.Error().What();
	PlainStream sender = std::move(senderResult.Value());

	auto sendTask = sender.SendRegistered(rb);
	auto sendResult = Drive(context, sendTask);
	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();
	EXPECT_EQ(sendResult.Value(), payloadLen);

	// 상대는 등록버퍼를 몰라도 되므로 plain recv 로 그대로 받는다.
	char recvBuf[64]{};
	const int got = ::recv(accepted.Handle(), recvBuf, static_cast<int>(payloadLen), 0);
	EXPECT_EQ(got, static_cast<int>(payloadLen));
	EXPECT_EQ(std::memcmp(recvBuf, payload, payloadLen), 0);
}

// ── PlainStream::SendRegistered — 소켓이 RIO 로 만들어지지 않았으면 일반 Send 로 투명 폴백한다 ──
TEST(PlainStreamTest, SendRegisteredFallsBackToPlainSendOnNonRioSocket)
{
	TestEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	const char payload[] = "fallback-plain-send";
	const std::size_t payloadLen = sizeof(payload) - 1;
	std::vector<byte_t> region(payloadLen);
	std::memcpy(region.data(), payload, payloadLen);

	auto regResult = RegisteredBuffer::Register(engine, std::span<byte_t>{ region });
	if (regResult.IsError() && regResult.Error().IsUnsupported()) GTEST_SKIP() << "engine has no registered buffer provider";
	ASSERT_TRUE(regResult.IsOk()) << regResult.Error().What();
	RegisteredBuffer rb = std::move(regResult.Value());

	// 이번엔 클라이언트를 일반(RIO 아닌) 소켓으로 생성 — Socket::SendZeroCopy 가 2단계에서
	// UNSUPPORTED 를 값으로 반환하고, PlainStream::SendRegistered 는 그걸 감지해 Send() 로 폴백해야 한다.
	auto pairResult = MakeConnectedStreamPair(context, /*_clientRegisteredIo=*/false);
	ASSERT_TRUE(pairResult.IsOk()) << pairResult.Error().What();
	auto [server, client] = std::move(pairResult.Value());

	auto sendTask = client.SendRegistered(rb);
	auto sendResult = Drive(context, sendTask);
	ASSERT_TRUE(sendResult.IsOk()) << sendResult.Error().What();
	EXPECT_EQ(sendResult.Value(), payloadLen);

	byte_t recvBuf[64]{};
	auto receiveTask = server.Receive(BufferView{ recvBuf, payloadLen });
	auto receiveResult = Drive(context, receiveTask);
	ASSERT_TRUE(receiveResult.IsOk()) << receiveResult.Error().What();
	EXPECT_EQ(receiveResult.Value(), payloadLen);
	EXPECT_EQ(std::memcmp(recvBuf, payload, payloadLen), 0);
}

#endif // _WIN32
