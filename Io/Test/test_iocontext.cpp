#include <gtest/gtest.h>

#if defined(_WIN32)

#include <winsock2.h>
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include "Io/Context/Context.h"
#include "Io/Engine/Iocp/IocpEngine.h"
#include "Base/Coroutine/Task.h"
#include "Time/Timer/TimerWheel.h"

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

		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		void_t await_suspend(const std::coroutine_handle<> _handle) noexcept
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

		[[nodiscard]] bool_t await_ready() const noexcept { return false; }
		void_t await_suspend(const std::coroutine_handle<> _handle) noexcept { context.Post(_handle); }
		void_t await_resume() const noexcept {}
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

// ── TimerWheel 통합: 루프가 타임아웃을 타이머 만료로 맞추고 Tick 한다 ──
TEST(IoContextTest, TimerWheelIntegration)
{
	IocpEngine engine;
	ne::time::TimerWheel wheel;
	Context context{ engine, &wheel };

	std::atomic<bool_t> fired{ false };
	(void_t)wheel.Schedule(std::chrono::milliseconds{ 50 }, [&] { fired.store(true, std::memory_order_release); });

	const auto start = std::chrono::steady_clock::now();
	while (!fired.load(std::memory_order_acquire) && std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) (void_t)context.RunOnce(std::chrono::milliseconds{ -1 }); // 타이머가 유효 타임아웃을 만든다

	EXPECT_TRUE(fired.load());
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	EXPECT_GE(elapsed, 50);
}

#endif // _WIN32
