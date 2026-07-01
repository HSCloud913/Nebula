//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#if defined(IS_POSIX)

#include <coroutine>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <liburing.h>
#include "Engine/IIoEngine.h"
#include "Queue/MpscQueue.h"
#include "Handle.h"
#include "Result.h"
#include "Error.h"
#include "Type.h"

BEGIN_NS(ne::io)
	// 파일 I/O 완료 컨텍스트. FileReadAwaitable / FileWriteAwaitable 이 소유.
	struct FileIoCtx
	{
		std::coroutine_handle<>              handle;
		ne::Result<std::size_t, ne::OsError> result{ ne::Result<std::size_t, ne::OsError>::Ok(0) };
	};

	// Linux io_uring + epoll 기반 통합 I/O 엔진.
	//
	// 소켓 이벤트 (IIoEngine — reactor):
	//   Watch/Unwatch → 내부 epollFd 에 등록/해제
	//   RunOnce       → epoll_wait → 소켓 콜백 디스패치
	//                             → completionEventFd 수신 시 DrainCompletions()
	//
	// 파일 I/O (proactor):
	//   SubmitRead/SubmitWrite → io_uring SQE 제출
	//   ThreadLoop            → CQE 처리 → completionQueue 에 handle 적재
	//                         → completionEventFd 에 신호
	//   DrainCompletions()    → RunOnce 스레드에서 handle.resume() 호출
	//
	// 모든 코루틴 resume 은 RunOnce() 호출 스레드에서 발생함이 보장된다.
	class IoUringEngine final : public IIoEngine
	{
		NEBULA_NON_COPYABLE_MOVABLE(IoUringEngine)

	public:
		explicit IoUringEngine(unsigned _queueDepth = 256) noexcept;
		~IoUringEngine() override;

	private:
		struct WatchEntry
		{
			uint32_t   events{};
			IoCallback callback;
		};

		using EpollFdHandle = ne::Handle<
			int_t,
			decltype([](const int_t _fd) { ::close(_fd); }),
			-1
		>;

		// 파일 I/O용 io_uring
		io_uring            ring{};
		bool_t              valid{ false };
		std::thread         thread;
		std::atomic<bool_t> running{ false };

		// 소켓 이벤트용 epoll
		EpollFdHandle                            epollFd;
		std::unordered_map<socket_t, WatchEntry> watches;

		// io_uring CQE 스레드 → RunOnce 스레드 브릿지
		ne::concurrency::MpscQueue<std::coroutine_handle<>> completionQueue;
		int completionEventFd{ -1 };

		ne::time::TimerWheel* timerWheel{ nullptr };

	public:
		[[nodiscard]] bool_t IsValid() const noexcept { return valid; }

		// ── Proactor (파일 I/O) ───────────────────────────────────────────────
		[[nodiscard]] ne::Result<void, ne::OsError>
			SubmitRead(int _fd, void* _buf, std::size_t _len, std::size_t _offset, FileIoCtx* _ctx) noexcept;

		[[nodiscard]] ne::Result<void, ne::OsError>
			SubmitWrite(int _fd, const void* _buf, std::size_t _len, std::size_t _offset, FileIoCtx* _ctx) noexcept;

		// ── IIoEngine: Reactor (소켓) + 이벤트 루프 구동 ─────────────────────
		[[nodiscard]] ne::Result<void, ne::OsError> Watch(socket_t _fd, uint32_t _events, IoCallback _cb) override;
		[[nodiscard]] ne::Result<void, ne::OsError> Unwatch(socket_t _fd) override;
		[[nodiscard]] ne::Result<void, ne::OsError> RunOnce(ne::int_t _timeoutMs = -1) override;
		void SetTimerWheel(ne::time::TimerWheel* _wheel) noexcept override { timerWheel = _wheel; }

	private:
		void ThreadLoop();
		void DrainCompletions() noexcept;

		static uint32_t ToEpollEvents(uint32_t _events) noexcept;
		static uint32_t FromEpollEvents(uint32_t _events) noexcept;
	};
END_NS

#endif // IS_POSIX
