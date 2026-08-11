#include <gtest/gtest.h>

#if defined(_WIN32)

#include "Base/WinsockApi.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <vector>
#include <thread>
#include "Io/Context.h"
#include "Io/Internal/Engine/Iocp/IocpEngine.h"
#include "Base/Coroutine/Task.h"
#include "Time/TimerQueue.h"

using namespace ne;
using namespace ne::io;

namespace
{
	// 완료를 기다리는 최소 awaitable — 프레임에 CompletionHandler 를 보관해 userData 로 넘긴다.
	// (정식 Level 2 awaitable 은 이후 Phase 에서 도입. 여기선 Level 1 디스패치만 검증.)
	struct SubmitAwaitable
	{
		Context& context;
		Request request;
		CompletionHandler handler{};

		[[nodiscard]] bool await_ready() const noexcept { return false; }

		void await_suspend(const std::coroutine_handle<> _handle) noexcept
		{
			handler.handle = _handle;
			request.userData = &handler;
			context.Engine().Submit(request);
		}

		[[nodiscard]] longlong_t await_resume() const noexcept { return handler.result; }
	};

	ne::Task<longlong_t> SubmitOp(Context& _context, Request _request) { co_return co_await SubmitAwaitable{ _context, _request }; }

	// 자기 자신을 Post 해 루프에서 재개되는지 검증.
	struct PostSelfAwaitable
	{
		Context& context;

		[[nodiscard]] bool await_ready() const noexcept { return false; }
		void await_suspend(const std::coroutine_handle<> _handle) noexcept { context.Post(_handle); }
		void await_resume() const noexcept {}
	};

	ne::Task<int_t> PostRoundTrip(Context& _context)
	{
		co_await PostSelfAwaitable{ _context };
		co_return 42;
	}

	template <typename T>
	T DriveUntilReady(Context& _context, ne::Task<T>& _task)
	{
		_task.Resume();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!_task.IsReady() && std::chrono::steady_clock::now() < deadline) (void_t)_context.RunOnce(std::chrono::milliseconds{ 50 });
		return _task.await_resume();
	}
}

// ── 엔진 완료가 대기 코루틴으로 디스패치되는가 (Level 1 핵심) ──
TEST(IoContextTest, DispatchesCompletionToCoroutine)
{
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	const lpcstr_t path = "test_iocontext_file.bin";
	const HANDLE file = ::CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_OVERLAPPED, nullptr);
	ASSERT_NE(file, INVALID_HANDLE_VALUE);

	const char payload[] = "level1-iocontext-dispatch";
	const std::size_t length = sizeof(payload) - 1;

	Request write{ .requestKind = RequestKind::WRITE, .handle = reinterpret_cast<ulonglong_t>(file), .buffer = const_cast<lpstr_t>(payload), .length = length, .offset = 0 };
	auto writeTask = SubmitOp(context, write);
	EXPECT_EQ(DriveUntilReady(context, writeTask), static_cast<longlong_t>(length));

	char buffer[64]{};
	Request read{ .requestKind = RequestKind::READ, .handle = reinterpret_cast<ulonglong_t>(file), .buffer = buffer, .length = length, .offset = 0 };
	auto readTask = SubmitOp(context, read);
	EXPECT_EQ(DriveUntilReady(context, readTask), static_cast<longlong_t>(length));
	EXPECT_EQ(std::memcmp(buffer, payload, length), 0);

	::CloseHandle(file);
	::DeleteFileA(path);
}

// ── Post → Wake → DrainPosted 로 코루틴이 루프에서 재개되는가 ──
TEST(IoContextTest, PostResumesOnLoop)
{
	IocpEngine engine;
	Context context{ engine };

	auto task = PostRoundTrip(context);
	EXPECT_EQ(DriveUntilReady(context, task), 42);
}

// ── TimerQueue 통합: 루프가 타임아웃을 타이머 만료로 맞추고 Tick 한다 ──
TEST(IoContextTest, TimerQueueIntegration)
{
	IocpEngine engine;
	ne::time::TimerQueue wheel;
	Context context{ engine, &wheel };

	std::atomic<bool_t> fired{ false };
	(void_t)wheel.Schedule(std::chrono::milliseconds{ 50 }, [&] { fired.store(true, std::memory_order_release); });

	const auto start = std::chrono::steady_clock::now();
	while (!fired.load(std::memory_order_acquire) && std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) (void_t)context.RunOnce(std::chrono::milliseconds{ -1 }); // 타이머가 유효 타임아웃을 만든다

	EXPECT_TRUE(fired.load());
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	EXPECT_GE(elapsed, 50);
}

// ── Start/Stop 경합: Stop 이 Start 의 루프 진입 직전에 도착해도 요청이 사라지지 않는다 ──
// 과거에는 "실행 중"/"정지 요청" 을 별개 bool 로 둬서, Start() 가 정지 플래그를 확인한 뒤 실행
// 플래그를 세우기 전에 Stop() 이 끼어들면 요청이 유실되고 루프가 영원히 돌았다(join 이 멈춤).
// 멈춤은 그대로 두면 테스트 자체가 걸려버리므로, 제한 시간 초과를 명시적 실패로 바꾼다.
// 창이 매우 좁아(플래그 확인 ~ 실행 플래그 설정 사이 몇 개 명령) 스레드 하나로는 거의 걸리지 않는다 —
// 워커를 여러 개 동시에 띄워 그중 하나가 그 지점에 있을 확률을 끌어올린다.
TEST(IoContextTest, StartStopRaceDoesNotHang)
{
	constexpr int_t WorkerCount = 8;

	for (int_t attempt = 0; attempt < 500; ++attempt)
	{
		std::vector<std::unique_ptr<IocpEngine>> engines;
		std::vector<std::unique_ptr<Context>> contexts;
		for (int_t i = 0; i < WorkerCount; ++i)
		{
			engines.push_back(std::make_unique<IocpEngine>());
			ASSERT_TRUE(engines.back()->IsValid());
			contexts.push_back(std::make_unique<Context>(*engines.back()));
		}

		std::vector<std::future<void>> pending;
		pending.reserve(contexts.size());
		for (const auto& context : contexts) pending.push_back(std::async(std::launch::async, [target = context.get()] { target->Start(); }));

		// 워커들이 Start() 의 어느 지점에 있든 정지 요청은 반드시 관측되어야 한다.
		for (const auto& context : contexts) context->Stop();

		for (std::size_t i = 0; i < pending.size(); ++i)
		{
			if (pending[i].wait_for(std::chrono::seconds(2)) == std::future_status::ready) continue;

			ADD_FAILURE() << "attempt " << attempt << ", worker " << i << ": Stop() 이 유실되어 Start() 루프가 빠져나오지 못했다";

			// 매달린 채 끝내면 future 소멸자가 영원히 기다리므로, 한 번 더 요청해 풀어 준다.
			for (const auto& context : contexts) context->Stop();
			for (auto& task : pending) task.wait();

			return;
		}
	}

	SUCCEED();
}

// ── Start/Stop 을 순차로 여러 번 재사용할 수 있다(정지 플래그가 다음 Start 를 막지 않는다) ──
TEST(IoContextTest, StartStopIsReusable)
{
	IocpEngine engine;
	ASSERT_TRUE(engine.IsValid());
	Context context{ engine };

	for (int_t cycle = 0; cycle < 5; ++cycle)
	{
		auto pending = std::async(std::launch::async, [&context] { context.Start(); });

		// 워커가 실제로 루프에 들어갔는지 확인한 뒤 정지시킨다.
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!context.IsRunning() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
		EXPECT_TRUE(context.IsRunning()) << "cycle " << cycle;

		context.Stop();
		ASSERT_EQ(pending.wait_for(std::chrono::seconds(2)), std::future_status::ready) << "cycle " << cycle;
		EXPECT_FALSE(context.IsRunning()) << "cycle " << cycle;
	}
}

#endif // _WIN32
