#include <gtest/gtest.h>
#include "AsyncFile.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

using namespace ne::io;
namespace fs = std::filesystem;

// 플랫폼별 엔진 타입 통일
#if defined(IS_POSIX)
    using TestEngine = IoUringEngine;
#elif defined(_WIN32)
    using TestEngine = FileIocpEngine;
#endif

// 비동기 작업 완료까지 최대 5초 대기. atomic flag 로 동기화.
static void WaitFor(const std::atomic<bool>& _flag,
                    std::chrono::milliseconds _timeout = std::chrono::seconds(5))
{
    const auto deadline = std::chrono::steady_clock::now() + _timeout;
    while (!_flag.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}



TEST(AsyncFileTest, CreateAndWrite)
{
    TestEngine engine;
    const ne::string_t path = "test_asyncfile_tmp.bin";

    auto cr = AsyncFile::Create(path, engine);
    ASSERT_TRUE(cr.IsOk()) << cr.Error().What();

    AsyncFile file = std::move(cr.Value());
    EXPECT_TRUE(file.IsOpen());

    const ne::byte_t data[] = { 1, 2, 3, 4, 5 };
    std::atomic<bool> done{ false };
    ne::Result<std::size_t, ne::OsError> result = ne::Result<std::size_t, ne::OsError>::Ok(0);

    auto task = [&]() -> ne::Task<void>
    {
        result = co_await file.Write(std::span<const ne::byte_t>(data, sizeof(data)), 0);
        done.store(true, std::memory_order_release);
    }();
    task.Resume();

    WaitFor(done);
    ASSERT_TRUE(done.load());
    EXPECT_TRUE(result.IsOk());

    (void)file.Close();
    fs::remove(path);
}

TEST(AsyncFileTest, OpenNonExistent)
{
    TestEngine engine;
    auto r = AsyncFile::Open("does_not_exist_12345.bin", engine);
    EXPECT_TRUE(r.IsError());
}

TEST(AsyncFileTest, WriteAndRead)
{
    TestEngine engine;
    const ne::string_t path = "test_asyncfile_rw.bin";

    // Write
    {
        auto cr = AsyncFile::Create(path, engine);
        ASSERT_TRUE(cr.IsOk());
        AsyncFile file = std::move(cr.Value());

        const ne::byte_t wdata[] = { 10, 20, 30, 40, 50 };
        std::atomic<bool> done{ false };
        ne::Result<std::size_t, ne::OsError> wr = ne::Result<std::size_t, ne::OsError>::Ok(0);

        auto wt = [&]() -> ne::Task<void>
        {
            wr = co_await file.Write(std::span<const ne::byte_t>(wdata, sizeof(wdata)), 0);
            done.store(true, std::memory_order_release);
        }();
        wt.Resume();

        WaitFor(done);
        ASSERT_TRUE(done.load());
        ASSERT_TRUE(wr.IsOk());
    }

    // Read back
    {
        auto or_ = AsyncFile::Open(path, engine, true);
        ASSERT_TRUE(or_.IsOk());
        AsyncFile file = std::move(or_.Value());

        ne::byte_t rbuf[5]{};
        std::atomic<bool> done{ false };
        ne::Result<std::size_t, ne::OsError> rr = ne::Result<std::size_t, ne::OsError>::Ok(0);

        auto rt = [&]() -> ne::Task<void>
        {
            rr = co_await file.Read(std::span<ne::byte_t>(rbuf, sizeof(rbuf)), 0);
            done.store(true, std::memory_order_release);
        }();
        rt.Resume();

        WaitFor(done);
        ASSERT_TRUE(done.load());
        ASSERT_TRUE(rr.IsOk());
        EXPECT_EQ(rr.Value(), 5u);
        EXPECT_EQ(rbuf[0], 10);
        EXPECT_EQ(rbuf[4], 50);
    }

    fs::remove(path);
}

// ─── 핵심 검증: 진짜 비동기인가 ────────────────────────────────────────────
// Read 가 co_await 에서 진짜로 suspend 되는지 확인한다.
// A(Read) 가 suspend 된 사이에 B(카운터 증가)가 실행돼야 한다.
// Read 가 동기(블로킹)였다면 taskA.Resume() 이 반환되기 전에 모든 I/O 가 끝나버려
// B 는 A 보다 먼저 실행될 기회 자체가 없다.
TEST(AsyncFileTest, TrueAsyncRead)
{
    TestEngine engine;
    const ne::string_t path = "test_asyncfile_trueasync.bin";

    // 4KB 테스트 파일 생성
    {
        auto cr = AsyncFile::Create(path, engine);
        ASSERT_TRUE(cr.IsOk());
        AsyncFile file = std::move(cr.Value());

        std::vector<ne::byte_t> wbuf(4096, 0xAB);
        std::atomic<bool> wdone{ false };

        auto wt = [&]() -> ne::Task<void>
        {
            (void)co_await file.Write(std::span<const ne::byte_t>(wbuf.data(), wbuf.size()), 0);
            wdone.store(true, std::memory_order_release);
        }();
        wt.Resume();

        WaitFor(wdone);
        ASSERT_TRUE(wdone.load());
    }

    auto or_ = AsyncFile::Open(path, engine, true);
    ASSERT_TRUE(or_.IsOk());
    AsyncFile file = std::move(or_.Value());

    // A: 파일 읽기 — co_await 에서 suspend 돼야 함
    std::vector<ne::byte_t> rbuf(4096);
    std::atomic<bool> aStarted{ false };  // Read 제출 후 signal
    std::atomic<bool> aDone{ false };     // Read 완료 후 signal
    ne::Result<std::size_t, ne::OsError> aResult = ne::Result<std::size_t, ne::OsError>::Ok(0);

    auto taskA = [&]() -> ne::Task<void>
    {
        aStarted.store(true, std::memory_order_release);
        aResult = co_await file.Read(std::span<ne::byte_t>(rbuf.data(), rbuf.size()), 0);
        aDone.store(true, std::memory_order_release);
    }();

    // A 시작: I/O 제출 후 suspend 됨. taskA.Resume() 은 즉시 반환.
    taskA.Resume();

    // ★ 핵심 단언: A.Resume() 직후 A 는 아직 완료되지 않아야 한다.
    // 동기(블로킹) pread/ReadFile 구현이었다면 Resume() 이 I/O 가 끝날 때까지 블로킹되어
    // 이 시점에 aDone 이 이미 true 가 된다 → EXPECT_FALSE 실패.
    // 매우 빠른 로컬 NVMe 에서 극히 드물게 타이밍 문제가 생길 수 있으나 설계 증명에 필수.
    EXPECT_FALSE(aDone.load(std::memory_order_acquire))
        << "A.Resume() must return before I/O completes — proves truly non-blocking async I/O";

    // B: A 가 suspend 된 사이에 실행
    // 동기(블로킹) 구현이었다면 taskA.Resume() 이 반환되지 않아 여기 도달 불가
    std::atomic<bool> bDone{ false };
    auto taskB = [&]() -> ne::Task<void>
    {
        bDone.store(true, std::memory_order_release);
        co_return;
    }();
    taskB.Resume();

    EXPECT_TRUE(bDone.load(std::memory_order_acquire))
        << "B must run while A is suspended (A's Resume() must return quickly)";

    // A 완료 대기
    WaitFor(aDone);

    EXPECT_TRUE(aDone.load(std::memory_order_acquire));
    EXPECT_TRUE(aResult.IsOk());
    EXPECT_EQ(rbuf[0], static_cast<ne::byte_t>(0xAB));

    fs::remove(path);
}
