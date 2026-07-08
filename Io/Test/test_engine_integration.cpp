//
// Created by hscloud on 26. 7. 1.
//
// 검증 시나리오:
//   1. 엔진이 소켓 이벤트와 파일 I/O 를 동시에 처리한다.
//   2. 모든 handle.resume() 이 RunOnce() 호출 스레드에서만 발생한다
//      (thread_id 비교로 검증).
//   3. 소켓 이벤트 + 파일 I/O 가 같은 엔진에서 경쟁 없이 동작한다.

#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>
#include "Engine/IIoEngine.h"
#include "File/AsyncFile.h"
#include "Coroutine/Task.h"

using namespace ne::io;
namespace fs = std::filesystem;

// ── 헬퍼: RunOnce() 구동 대기 ────────────────────────────────────────────────
static void DriveEngine(IIoEngine& _engine, const std::atomic<bool>& _done,
                        std::chrono::milliseconds _timeout = std::chrono::seconds(5))
{
    const auto deadline = std::chrono::steady_clock::now() + _timeout;
    while (!_done.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
        _engine.RunOnce(10);
}

// ─────────────────────────────────────────────────────────────────────────────
#if defined(IS_POSIX)

#include <sys/socket.h>
#include <unistd.h>
#include "Engine/Epoll/EpollEngine.h"
#include "Engine/IoUring/IoUringEngine.h"
#include "Coroutine/Awaitable.h"

// ── EpollEngine: 소켓 이벤트 콜백 디스패치 ───────────────────────────────────
TEST(IoEngineIntegration, EpollSocketDispatch)
{
    EpollEngine engine;
    ASSERT_TRUE(engine.IsValid());

    int fds[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    std::atomic<bool> fired{ false };
    auto r = engine.Watch(
        static_cast<socket_t>(fds[0]),
        IoEvent::Read | IoEvent::HangUp,
        [&](socket_t, uint32_t) { fired.store(true, std::memory_order_release); });
    ASSERT_TRUE(r.IsOk());

    const char msg = 'x';
    ASSERT_EQ(::write(fds[1], &msg, 1), 1);

    DriveEngine(engine, fired, std::chrono::milliseconds(500));
    EXPECT_TRUE(fired.load()) << "Watch callback must fire after write";

    ::close(fds[0]);
    ::close(fds[1]);
}

// ── EpollEngine: 같은 fd 에 Read/Write 를 동시에 Watch — 서로 독립적으로 동작해야 함 ──
TEST(IoEngineIntegration, EpollConcurrentReadWriteWatch)
{
    EpollEngine engine;
    ASSERT_TRUE(engine.IsValid());

    int fds[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    std::atomic<bool> readFired{ false };
    std::atomic<bool> writeFired{ false };

    auto readRes = engine.Watch(
        static_cast<socket_t>(fds[0]),
        IoEvent::Read | IoEvent::HangUp,
        [&](socket_t, uint32_t) { readFired.store(true, std::memory_order_release); });
    ASSERT_TRUE(readRes.IsOk());

    // Read 가 이미 활성화된 fd 에 Write 를 추가로 Watch — 반대 방향 슬롯을 덮어쓰지 않아야 함.
    auto writeRes = engine.Watch(
        static_cast<socket_t>(fds[0]),
        IoEvent::Write | IoEvent::Error,
        [&](socket_t, uint32_t) { writeFired.store(true, std::memory_order_release); });
    ASSERT_TRUE(writeRes.IsOk()) << "Write watch must succeed even though Read is already active on the same fd";

    // 소켓은 연결 직후 곧바로 쓰기 가능하므로 write 콜백이 먼저 온다 — 그 사이 read 슬롯이
    // 살아있는지(덮어써지지 않았는지) 확인한다.
    DriveEngine(engine, writeFired, std::chrono::milliseconds(500));
    EXPECT_TRUE(writeFired.load()) << "Write watch must fire independently of the read watch";
    EXPECT_FALSE(readFired.load()) << "Read watch must not fire before data arrives";

    const char msg = 'x';
    ASSERT_EQ(::write(fds[1], &msg, 1), 1);

    DriveEngine(engine, readFired, std::chrono::milliseconds(500));
    EXPECT_TRUE(readFired.load()) << "Read watch must still fire after the write watch fired independently";

    ::close(fds[0]);
    ::close(fds[1]);
}

// ── IoUringEngine: handle.resume() 이 RunOnce 스레드에서만 발생 ──────────────
TEST(IoEngineIntegration, IoUringResumeInRunOnceThread)
{
    IoUringEngine engine;
    ASSERT_TRUE(engine.IsValid());

    const ne::string_t path = "test_integration_resume_thread.bin";

    // 테스트 파일 준비
    {
        auto cr = AsyncFile::Create(path, engine);
        ASSERT_TRUE(cr.IsOk());
        AsyncFile fw = std::move(cr.Value());

        const ne::byte_t data[] = { 0xCA, 0xFE, 0xBA, 0xBE };
        std::atomic<bool> wdone{ false };
        auto wt = [&]() -> ne::Task<void>
        {
            (void)co_await fw.Write(std::span<const ne::byte_t>(data, sizeof(data)), 0);
            wdone.store(true, std::memory_order_release);
        }();
        wt.Resume();
        DriveEngine(engine, wdone);
        ASSERT_TRUE(wdone.load()) << "Write must complete";
    }

    auto fr = AsyncFile::Open(path, engine, true);
    ASSERT_TRUE(fr.IsOk());
    AsyncFile file = std::move(fr.Value());

    std::atomic<bool> readDone{ false };
    std::thread::id resumeThreadId;
    ne::byte_t rbuf[4]{};

    auto task = [&]() -> ne::Task<void>
    {
        (void)co_await file.Read(std::span<ne::byte_t>(rbuf, sizeof(rbuf)), 0);
        resumeThreadId = std::this_thread::get_id();
        readDone.store(true, std::memory_order_release);
    }();
    task.Resume(); // I/O 제출 후 suspend

    // RunOnce 를 전용 스레드에서 구동 — resume 이 이 스레드에서 발생해야 함
    std::thread::id engineThreadId;
    std::atomic<bool> engineReady{ false };
    std::thread engineThread([&]
    {
        engineThreadId = std::this_thread::get_id();
        engineReady.store(true, std::memory_order_release);
        DriveEngine(engine, readDone);
    });

    while (!engineReady.load(std::memory_order_acquire))
        std::this_thread::yield();

    engineThread.join();

    ASSERT_TRUE(readDone.load()) << "File read must complete";
    EXPECT_EQ(resumeThreadId, engineThreadId)
        << "handle.resume() must be called from the RunOnce() thread";
    EXPECT_EQ(rbuf[0], static_cast<ne::byte_t>(0xCA));

    fs::remove(path);
}

// ── IoUringEngine: 소켓 이벤트 + 파일 I/O 동시 처리 ─────────────────────────
TEST(IoEngineIntegration, IoUringSocketAndFileConcurrent)
{
    IoUringEngine engine;
    ASSERT_TRUE(engine.IsValid());

    // 소켓 페어 등록
    int fds[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    std::atomic<int> socketFired{ 0 };
    auto wr = engine.Watch(
        static_cast<socket_t>(fds[0]),
        IoEvent::Read | IoEvent::HangUp,
        [&](socket_t, uint32_t) { socketFired.fetch_add(1, std::memory_order_release); });
    ASSERT_TRUE(wr.IsOk());

    // 소켓에 데이터 전송 (Watch 콜백 유발)
    const char ping = 'p';
    ASSERT_EQ(::write(fds[1], &ping, 1), 1);

    // 파일 Read 코루틴 준비
    const ne::string_t path = "test_integration_concurrent.bin";
    {
        auto cr = AsyncFile::Create(path, engine);
        ASSERT_TRUE(cr.IsOk());
        AsyncFile fw = std::move(cr.Value());
        const ne::byte_t wdata[] = { 1, 2, 3, 4 };
        std::atomic<bool> wdone{ false };
        auto wt = [&]() -> ne::Task<void>
        {
            (void)co_await fw.Write(std::span<const ne::byte_t>(wdata, sizeof(wdata)), 0);
            wdone.store(true, std::memory_order_release);
        }();
        wt.Resume();
        DriveEngine(engine, wdone);
        ASSERT_TRUE(wdone.load());
    }

    auto fr = AsyncFile::Open(path, engine, true);
    ASSERT_TRUE(fr.IsOk());
    AsyncFile file = std::move(fr.Value());
    std::atomic<bool> fileDone{ false };
    ne::byte_t rbuf[4]{};

    auto fileTask = [&]() -> ne::Task<void>
    {
        (void)co_await file.Read(std::span<ne::byte_t>(rbuf, sizeof(rbuf)), 0);
        fileDone.store(true, std::memory_order_release);
    }();
    fileTask.Resume();

    // 소켓 + 파일 양쪽 모두 같은 RunOnce 루프에서 처리됨
    DriveEngine(engine, fileDone, std::chrono::milliseconds(500));

    EXPECT_TRUE(fileDone.load()) << "File read must complete";
    EXPECT_GT(socketFired.load(), 0) << "Socket event must have fired";

    ::close(fds[0]);
    ::close(fds[1]);
    fs::remove(path);
}

// ── 진행 중 Proactor 수신을 Task 중도 폐기 → 완료가 도착해도 UAF/재개 없어야 함 (1b) ──
// 수신 버퍼는 코루틴 프레임 밖(테스트 프레임)에 둔다 — 검증 대상은 IoContext(완료 통지) 수명이지
// 버퍼 수명이 아니다. 폐기 후 완료가 도착하면 엔진은 resume 없이 IoContext 를 해제해야 한다.
TEST(IoEngineIntegration, IoUringAbandonedProactorReceiveIsSafe)
{
    IoUringEngine engine;
    ASSERT_TRUE(engine.IsValid());

    int fds[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    std::array<ne::byte_t, 16> rbuf{};
    std::atomic<bool> resumed{ false };

    {
        auto task = [&]() -> ne::Task<void>
        {
            (void)co_await ReceiveSubmitAwaitable{ engine, static_cast<socket_t>(fds[0]), rbuf.data(), rbuf.size() };
            resumed.store(true, std::memory_order_release); // abandoned 면 절대 실행되면 안 됨
        }();
        task.Resume(); // 수신 제출 + suspend (데이터 없음 → pending)
    } // task 소멸 → 프레임 파괴 → IoContextHolder 소멸자가 abandoned=true

    const char msg[4] = { 'a', 'b', 'c', 'd' };
    ASSERT_EQ(::write(fds[1], msg, sizeof(msg)), static_cast<ssize_t>(sizeof(msg)));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) engine.RunOnce(10);

    EXPECT_FALSE(resumed.load()) << "abandoned coroutine must never resume";

    ::close(fds[0]);
    ::close(fds[1]);
}

// EpollEngine 은 reactor-emulated — abandon 시 콜백이 recv 를 건너뛰고 IoContext 만 해제하는 분기를 검증.
TEST(IoEngineIntegration, EpollAbandonedProactorReceiveIsSafe)
{
    EpollEngine engine;
    ASSERT_TRUE(engine.IsValid());

    int fds[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    std::array<ne::byte_t, 16> rbuf{};
    std::atomic<bool> resumed{ false };

    {
        auto task = [&]() -> ne::Task<void>
        {
            (void)co_await ReceiveSubmitAwaitable{ engine, static_cast<socket_t>(fds[0]), rbuf.data(), rbuf.size() };
            resumed.store(true, std::memory_order_release);
        }();
        task.Resume();
    }

    const char msg[4] = { 'a', 'b', 'c', 'd' };
    ASSERT_EQ(::write(fds[1], msg, sizeof(msg)), static_cast<ssize_t>(sizeof(msg)));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) engine.RunOnce(10);

    EXPECT_FALSE(resumed.load()) << "abandoned coroutine must never resume";

    ::close(fds[0]);
    ::close(fds[1]);
}

// ─────────────────────────────────────────────────────────────────────────────
#elif defined(_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>
#include "Engine/Iocp/IocpEngine.h"
#include "Coroutine/Awaitable.h"
#include "Buffer/BufferBlock.h"
#include "Allocator/PoolAllocator.h"

// 로컬루프백 TCP 소켓 페어 생성 헬퍼
static bool MakeSocketPair(SOCKET& _s1, SOCKET& _s2)
{
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;

    if (::bind(listener, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(listener, 1) != 0)
    {
        ::closesocket(listener);
        return false;
    }

    int addrLen = sizeof(addr);
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addrLen);

    _s1 = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_s1 == INVALID_SOCKET || ::connect(_s1, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        ::closesocket(listener);
        return false;
    }

    _s2 = ::accept(listener, nullptr, nullptr);
    ::closesocket(listener);
    return _s2 != INVALID_SOCKET;
}

// ── IocpEngine: 소켓 이벤트 콜백 디스패치 ─────────────────────────────────────
TEST(IoEngineIntegration, IocpSocketDispatch)
{
    IocpEngine engine;
    ASSERT_TRUE(engine.IsValid());

    SOCKET s1 = INVALID_SOCKET;
    SOCKET s2 = INVALID_SOCKET;
    ASSERT_TRUE(MakeSocketPair(s1, s2));

    std::atomic<bool> fired{ false };
    auto r = engine.Watch(
        static_cast<socket_t>(s1),
        IoEvent::Read | IoEvent::HangUp,
        [&](socket_t, uint32_t) { fired.store(true, std::memory_order_release); });
    ASSERT_TRUE(r.IsOk());

    const char msg = 'x';
    ASSERT_NE(::send(s2, &msg, 1, 0), SOCKET_ERROR);

    DriveEngine(engine, fired, std::chrono::milliseconds(1000));
    EXPECT_TRUE(fired.load()) << "Watch callback must fire after send";

    ::closesocket(s1);
    ::closesocket(s2);
}

// ── IocpEngine: 같은 fd 에 Read/Write 를 동시에 Watch — 서로 독립적으로 동작해야 함 ──
TEST(IoEngineIntegration, IocpConcurrentReadWriteWatch)
{
    IocpEngine engine;
    ASSERT_TRUE(engine.IsValid());

    SOCKET s1 = INVALID_SOCKET;
    SOCKET s2 = INVALID_SOCKET;
    ASSERT_TRUE(MakeSocketPair(s1, s2));

    std::atomic<bool> readFired{ false };
    std::atomic<bool> writeFired{ false };

    auto readRes = engine.Watch(
        static_cast<socket_t>(s1),
        IoEvent::Read | IoEvent::HangUp,
        [&](socket_t, uint32_t) { readFired.store(true, std::memory_order_release); });
    ASSERT_TRUE(readRes.IsOk());

    // Read 가 이미 활성화된 fd(s1)에 Write 를 추가로 Watch — WatchSlots 분리 이전에는 나중에
    // 등록한 방향의 슬롯에 fd 가 채워지지 않아 WSASend 가 소켓 0번을 대상으로 호출되며
    // 즉시 실패하는 버그가 있었다.
    auto writeRes = engine.Watch(
        static_cast<socket_t>(s1),
        IoEvent::Write | IoEvent::Error,
        [&](socket_t, uint32_t) { writeFired.store(true, std::memory_order_release); });
    ASSERT_TRUE(writeRes.IsOk()) << "Write watch must succeed even though Read is already active on the same fd";

    // 소켓은 연결 직후 곧바로 쓰기 가능하므로 write 콜백이 먼저 온다 — 그 사이 read 슬롯이
    // 살아있는지(덮어써지지 않았는지) 확인한다.
    DriveEngine(engine, writeFired, std::chrono::milliseconds(1000));
    EXPECT_TRUE(writeFired.load()) << "Write watch must fire independently of the read watch";
    EXPECT_FALSE(readFired.load()) << "Read watch must not fire before data arrives";

    const char msg = 'y';
    ASSERT_NE(::send(s2, &msg, 1, 0), SOCKET_ERROR);

    DriveEngine(engine, readFired, std::chrono::milliseconds(1000));
    EXPECT_TRUE(readFired.load()) << "Read watch must still fire after the write watch fired independently";

    ::closesocket(s1);
    ::closesocket(s2);
}

// ── IocpEngine: handle.resume() 이 RunOnce 스레드에서만 발생 ─────────────────
TEST(IoEngineIntegration, IocpResumeInRunOnceThread)
{
    IocpEngine engine;
    ASSERT_TRUE(engine.IsValid());

    const ne::string_t path = "test_integration_resume_thread.bin";

    {
        auto cr = AsyncFile::Create(path, engine);
        ASSERT_TRUE(cr.IsOk());
        AsyncFile fw = std::move(cr.Value());

        const ne::byte_t data[] = { 0xCA, 0xFE, 0xBA, 0xBE };
        std::atomic<bool> wdone{ false };
        auto wt = [&]() -> ne::Task<void>
        {
            (void)co_await fw.Write(std::span<const ne::byte_t>(data, sizeof(data)), 0);
            wdone.store(true, std::memory_order_release);
        }();
        wt.Resume();
        DriveEngine(engine, wdone);
        ASSERT_TRUE(wdone.load()) << "Write must complete";
    }

    auto fr = AsyncFile::Open(path, engine, true);
    ASSERT_TRUE(fr.IsOk());
    AsyncFile file = std::move(fr.Value());

    std::atomic<bool> readDone{ false };
    std::thread::id resumeThreadId;
    ne::byte_t rbuf[4]{};

    auto task = [&]() -> ne::Task<void>
    {
        (void)co_await file.Read(std::span<ne::byte_t>(rbuf, sizeof(rbuf)), 0);
        resumeThreadId = std::this_thread::get_id();
        readDone.store(true, std::memory_order_release);
    }();
    task.Resume();

    std::thread::id engineThreadId;
    std::atomic<bool> engineReady{ false };
    std::thread engineThread([&]
    {
        engineThreadId = std::this_thread::get_id();
        engineReady.store(true, std::memory_order_release);
        DriveEngine(engine, readDone);
    });

    while (!engineReady.load(std::memory_order_acquire))
        std::this_thread::yield();

    engineThread.join();

    ASSERT_TRUE(readDone.load()) << "File read must complete";
    EXPECT_EQ(resumeThreadId, engineThreadId)
        << "handle.resume() must be called from the RunOnce() thread";
    EXPECT_EQ(rbuf[0], static_cast<ne::byte_t>(0xCA));

    (void)file.Close();
    fs::remove(path);
}

// ── IocpEngine: 소켓 이벤트 + 파일 I/O 동시 처리 ────────────────────────────
TEST(IoEngineIntegration, IocpSocketAndFileConcurrent)
{
    IocpEngine engine;
    ASSERT_TRUE(engine.IsValid());

    SOCKET s1 = INVALID_SOCKET;
    SOCKET s2 = INVALID_SOCKET;
    ASSERT_TRUE(MakeSocketPair(s1, s2));

    std::atomic<int> socketFired{ 0 };
    auto wr = engine.Watch(
        static_cast<socket_t>(s1),
        IoEvent::Read | IoEvent::HangUp,
        [&](socket_t, uint32_t) { socketFired.fetch_add(1, std::memory_order_release); });
    ASSERT_TRUE(wr.IsOk());

    const char msg = 'z';
    ASSERT_NE(::send(s2, &msg, 1, 0), SOCKET_ERROR);

    const ne::string_t path = "test_integration_concurrent.bin";
    {
        auto cr = AsyncFile::Create(path, engine);
        ASSERT_TRUE(cr.IsOk());
        AsyncFile fw = std::move(cr.Value());
        const ne::byte_t wdata[] = { 1, 2, 3, 4 };
        std::atomic<bool> wdone{ false };
        auto wt = [&]() -> ne::Task<void>
        {
            (void)co_await fw.Write(std::span<const ne::byte_t>(wdata, sizeof(wdata)), 0);
            wdone.store(true, std::memory_order_release);
        }();
        wt.Resume();
        DriveEngine(engine, wdone);
        ASSERT_TRUE(wdone.load());
    }

    auto fr = AsyncFile::Open(path, engine, true);
    ASSERT_TRUE(fr.IsOk());
    AsyncFile file = std::move(fr.Value());
    std::atomic<bool> fileDone{ false };
    ne::byte_t rbuf[4]{};

    auto fileTask = [&]() -> ne::Task<void>
    {
        (void)co_await file.Read(std::span<ne::byte_t>(rbuf, sizeof(rbuf)), 0);
        fileDone.store(true, std::memory_order_release);
    }();
    fileTask.Resume();

    DriveEngine(engine, fileDone, std::chrono::milliseconds(1000));

    EXPECT_TRUE(fileDone.load()) << "File read must complete";
    EXPECT_GT(socketFired.load(), 0) << "Socket event must have fired";

    (void)file.Close();
    ::closesocket(s1);
    ::closesocket(s2);
    fs::remove(path);
}

// ── IocpEngine: SubmitTransmitFile — Proactor + zero-copy 파일→소켓 전송 ────────
TEST(IoEngineIntegration, IocpTransmitFile)
{
    IocpEngine engine;
    ASSERT_TRUE(engine.IsValid());

    SOCKET s1 = INVALID_SOCKET;
    SOCKET s2 = INVALID_SOCKET;
    ASSERT_TRUE(MakeSocketPair(s1, s2));

    const ne::string_t path = "test_integration_transmitfile.bin";
    const ne::byte_t payload[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02 };

    HANDLE fileHandle = ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    ASSERT_NE(fileHandle, INVALID_HANDLE_VALUE);

    // 파일에 페이로드를 기록(테스트 준비 단계, 아직 IOCP 미등록 — GetOverlappedResult 로 대기).
    {
        OVERLAPPED ov{};
        DWORD written = 0;
        const BOOL ok = ::WriteFile(fileHandle, payload, sizeof(payload), nullptr, &ov);
        ASSERT_TRUE(ok || ::GetLastError() == ERROR_IO_PENDING);
        ASSERT_TRUE(::GetOverlappedResult(fileHandle, &ov, &written, TRUE));
        ASSERT_EQ(written, sizeof(payload));
    }

    ASSERT_TRUE(engine.RegisterFileHandle(fileHandle).IsOk());

    std::atomic<bool> done{ false };
    ne::Result<std::size_t, ne::OsError> result = ne::Result<std::size_t, ne::OsError>::Ok(0);

    auto task = [&]() -> ne::Task<void>
    {
        result = co_await ne::io::TransmitFileSubmitAwaitable{ engine, static_cast<socket_t>(s1), fileHandle, 0, sizeof(payload) };
        done.store(true, std::memory_order_release);
    }();
    task.Resume();

    DriveEngine(engine, done, std::chrono::milliseconds(2000));
    ASSERT_TRUE(done.load());
    ASSERT_TRUE(result.IsOk()) << result.Error().What();
    EXPECT_EQ(result.Value(), sizeof(payload));

    char recvBuf[sizeof(payload)]{};
    int totalReceived = 0;
    while (totalReceived < static_cast<int>(sizeof(payload)))
    {
        const int n = ::recv(s2, recvBuf + totalReceived, sizeof(payload) - totalReceived, 0);
        ASSERT_GT(n, 0);
        totalReceived += n;
    }
    EXPECT_EQ(std::memcmp(recvBuf, payload, sizeof(payload)), 0);

    ::CloseHandle(fileHandle);
    ::closesocket(s1);
    ::closesocket(s2);
    fs::remove(path);
}

// ── IocpEngine: 진행 중 Proactor 수신을 Task 중도 폐기 → 완료가 도착해도 UAF/재개 없어야 함 (1b) ──
// 수신 버퍼는 코루틴 프레임 밖(테스트 프레임)에 둔다 — 검증 대상은 IoContext(완료 통지) 수명이지
// 버퍼 수명이 아니다. 폐기 후 WSARecv 완료가 IOCP 로 통지되면 엔진은 resume 없이 IoContext 를 해제해야 한다.
TEST(IoEngineIntegration, IocpAbandonedProactorReceiveIsSafe)
{
    IocpEngine engine;
    ASSERT_TRUE(engine.IsValid());

    SOCKET s1 = INVALID_SOCKET;
    SOCKET s2 = INVALID_SOCKET;
    ASSERT_TRUE(MakeSocketPair(s1, s2));

    std::array<ne::byte_t, 16> rbuf{};
    std::atomic<bool> resumed{ false };

    {
        auto task = [&]() -> ne::Task<void>
        {
            (void)co_await ReceiveSubmitAwaitable{ engine, static_cast<socket_t>(s1), rbuf.data(), rbuf.size() };
            resumed.store(true, std::memory_order_release); // abandoned 면 절대 실행되면 안 됨
        }();
        task.Resume(); // WSARecv 제출 + suspend (데이터 없음 → pending)
    } // task 소멸 → 프레임 파괴 → IoContextHolder 소멸자가 abandoned=true

    const char msg[4] = { 'a', 'b', 'c', 'd' };
    ASSERT_NE(::send(s2, msg, sizeof(msg), 0), SOCKET_ERROR);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) engine.RunOnce(10);

    EXPECT_FALSE(resumed.load()) << "abandoned coroutine must never resume";

    ::closesocket(s1);
    ::closesocket(s2);
}

// ── #4: Proactor 수신 버퍼는 op 완료 전 호출자가 ref 를 놓아도 살아있어야 한다 ──
// 커널이 비동기로 write 하는 버퍼가 완료 전 풀로 반납되면 UAF. IoContext 가 잡은 ref 로
// op 진행 중에는 살아있고, 완료 후 정확히 반납되는지를 pool.Available() 로 직접 검증.
TEST(IoEngineIntegration, IocpProactorBufferOutlivesCallerRelease)
{
    IocpEngine engine;
    ASSERT_TRUE(engine.IsValid());

    SOCKET s1 = INVALID_SOCKET;
    SOCKET s2 = INVALID_SOCKET;
    ASSERT_TRUE(MakeSocketPair(s1, s2));

    ne::memory::PoolAllocator pool(256, 4);
    auto blockRes = BufferBlock::Acquire(pool, 64);
    ASSERT_TRUE(blockRes.IsOk());
    BufferBlock* block = blockRes.Value(); // refCount 1 (호출자 소유)
    ASSERT_EQ(pool.Available(), 3u) << "one block acquired";

    std::atomic<bool> done{ false };
    ne::Result<std::size_t, ne::OsError> result = ne::Result<std::size_t, ne::OsError>::Ok(0);

    auto task = [&]() -> ne::Task<void>
    {
        const auto span = block->Data();
        result = co_await ReceiveSubmitAwaitable{ engine, static_cast<socket_t>(s1), span.data(), span.size(), block };
        done.store(true, std::memory_order_release);
    }();
    task.Resume(); // WSARecv 제출 → awaitable 이 block 을 AddRef (refCount 2)

    // 호출자가 즉시 자기 ref 를 놓는다 — 수정 전이라면 완료 전에 풀로 반납돼(커널이 freed 버퍼에 write) UAF.
    block->Release();

    // op 진행 중 — IoContext 가 ref 를 쥐고 있으므로 블록은 아직 살아있어야 한다(반납 X).
    EXPECT_EQ(pool.Available(), 3u) << "buffer must stay alive while the proactor op is in flight";

    const char msg[] = { 'h', 'e', 'l', 'l', 'o' };
    ASSERT_NE(::send(s2, msg, sizeof(msg), 0), SOCKET_ERROR);

    DriveEngine(engine, done, std::chrono::milliseconds(2000));
    ASSERT_TRUE(done.load());
    ASSERT_TRUE(result.IsOk()) << result.Error().What();
    EXPECT_EQ(result.Value(), sizeof(msg));

    // 완료로 awaitable(→ IoContext) 소멸 → 마지막 ref 해제 → 블록이 풀로 반납됐어야 한다.
    EXPECT_EQ(pool.Available(), 4u) << "buffer must be released back to the pool after completion";

    ::closesocket(s1);
    ::closesocket(s2);
}

// ── IocpEngine: RIO 등록 버퍼 provider — capability 광고 + register/unregister ──
TEST(IoEngineIntegration, IocpRegisteredBufferProvider)
{
    IocpEngine engine;
    ASSERT_TRUE(engine.IsValid());

    // capability 에 RegisteredIo 가 광고돼야 하고, provider 접근자가 non-null 이어야 한다.
    EXPECT_TRUE(HasCapability(engine.Capabilities(), IoCapability::RegisteredIo));
    auto* provider = engine.AsRegisteredBufferProvider();
    ASSERT_NE(provider, nullptr);

    // 실제 등록 → 유효 핸들 (lazy 로 RIO 테이블 + 공유 CQ 가 이때 초기화된다).
    std::array<ne::byte_t, 4096> region{};
    auto reg = provider->RegisterBuffer(std::span<ne::byte_t>{ region });
    ASSERT_TRUE(reg.IsOk()) << reg.Error().What();
    EXPECT_TRUE(reg.Value().IsValid());

    // 해제는 크래시/에러 없이 수행돼야 한다.
    provider->UnregisterBuffer(reg.Value());

    // 빈 영역은 InvalidBuffer 로 거부.
    auto empty = provider->RegisterBuffer(std::span<ne::byte_t>{});
    ASSERT_TRUE(empty.IsError());
    EXPECT_EQ(empty.Error().Kind(), IoErrorKind::INVALID_BUFFER);
}

#endif // platform
