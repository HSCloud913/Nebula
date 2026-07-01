#include <gtest/gtest.h>
#include "File/AsyncFile.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <vector>

using namespace ne::io;
namespace fs = std::filesystem;

#if defined(IS_POSIX)
    using TestEngine = IoUringEngine;
#elif defined(_WIN32)
    using TestEngine = IocpEngine;
#endif

// RunOnce() 를 구동해 done 플래그 대기.
// 기존 WaitFor(sleep 폴링) 대신 사용 — 엔진이 완료 통지를 RunOnce 스레드로만
// 전달하므로 RunOnce 를 직접 호출해야 코루틴이 재개된다.
static void DriveEngine(ne::io::IIoEngine& _engine, const std::atomic<bool>& _done,
                        std::chrono::milliseconds _timeout = std::chrono::seconds(5))
{
    const auto deadline = std::chrono::steady_clock::now() + _timeout;
    while (!_done.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
        _engine.RunOnce(10);
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

    DriveEngine(engine, done);
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

        DriveEngine(engine, done);
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

        DriveEngine(engine, done);
        ASSERT_TRUE(done.load());
        ASSERT_TRUE(rr.IsOk());
        EXPECT_EQ(rr.Value(), 5u);
        EXPECT_EQ(rbuf[0], 10);
        EXPECT_EQ(rbuf[4], 50);
    }

    fs::remove(path);
}

// ─── 핵심 검증: 진짜 비동기인가 ────────────────────────────────────────────
// 완료 통지는 RunOnce() 스레드로만 전달되므로, taskA.Resume() 직후
// RunOnce 를 한 번도 호출하지 않은 시점에서 aDone 은 반드시 false 다.
// 이는 "비동기 suspend 증명"이며 I/O 속도와 무관하게 항상 성립한다.
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

        DriveEngine(engine, wdone);
        ASSERT_TRUE(wdone.load());
    }

    auto or_ = AsyncFile::Open(path, engine, true);
    ASSERT_TRUE(or_.IsOk());
    AsyncFile file = std::move(or_.Value());

    std::vector<ne::byte_t> rbuf(4096);
    std::atomic<bool> aDone{ false };
    ne::Result<std::size_t, ne::OsError> aResult = ne::Result<std::size_t, ne::OsError>::Ok(0);

    auto taskA = [&]() -> ne::Task<void>
    {
        aResult = co_await file.Read(std::span<ne::byte_t>(rbuf.data(), rbuf.size()), 0);
        aDone.store(true, std::memory_order_release);
    }();

    // I/O 제출 — co_await 에서 즉시 suspend 됨
    taskA.Resume();

    // ★ RunOnce 를 한 번도 호출하지 않은 시점에서 aDone 은 false 여야 한다.
    // 엔진은 완료 재개를 RunOnce() 에서만 수행하므로 I/O 속도에 무관하게 항상 성립한다.
    EXPECT_FALSE(aDone.load(std::memory_order_acquire))
        << "co_await must suspend before RunOnce() is called";

    // B: A 가 suspend 된 동안 실행 가능
    std::atomic<bool> bDone{ false };
    auto taskB = [&]() -> ne::Task<void>
    {
        bDone.store(true, std::memory_order_release);
        co_return;
    }();
    taskB.Resume();

    EXPECT_TRUE(bDone.load(std::memory_order_acquire))
        << "B must run while A is suspended";

    // RunOnce 구동 → A 재개
    DriveEngine(engine, aDone);

    EXPECT_TRUE(aDone.load(std::memory_order_acquire));
    EXPECT_TRUE(aResult.IsOk());
    EXPECT_EQ(rbuf[0], static_cast<ne::byte_t>(0xAB));

    fs::remove(path);
}
