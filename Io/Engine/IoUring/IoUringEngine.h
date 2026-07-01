//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#if defined(IS_POSIX)

#include <coroutine>
#include <unordered_map>
#include <liburing.h>
#include "Engine/IIoEngine.h"
#include "Result.h"
#include "Error.h"
#include "Type.h"

BEGIN_NS(ne::io)
	// Linux io_uring 단일 경로 통합 I/O 엔진.
	//
	// 소켓 Reactor (IIoEngine — POLL_ADD):
	//   Watch   → IORING_OP_POLL_ADD SQE 제출 (one-shot)
	//   Unwatch → IORING_OP_POLL_REMOVE SQE 제출 (best-effort)
	//
	// 소켓 Proactor (IIoEngine — RECV/SEND):
	//   SubmitRecv/SubmitSend → IORING_OP_RECV/SEND, user_data = IoCtx*
	//
	// 파일 Proactor (IIoEngine 외부 — AsyncFile 전용):
	//   SubmitRead/SubmitWrite → IORING_OP_READ/WRITE, user_data = IoCtx*
	//
	// RunOnce: io_uring_wait_cqe_timeout → io_uring_peek_batch_cqe → CQE 분류:
	//   user_data bit63=1  → 소켓 POLL_ADD CQE → watches 맵 조회 후 콜백 디스패치
	//   user_data bit63=0  → IoCtx* (소켓 proactor or 파일) → result 저장 → handle.resume()
	//
	// x86-64 사용자 공간 주소는 최대 2^47 이므로 비트 47 이상은 항상 0.
	class IoUringEngine final : public IIoEngine<>
	{
		NEBULA_NON_COPYABLE_MOVABLE(IoUringEngine)

	public:
		explicit IoUringEngine(unsigned _queueDepth = 256) noexcept;
		~IoUringEngine() override;

	private:
		struct WatchEntry
		{
			uint32_t   generation{};
			uint32_t   events{};
			IoCallback callback;
		};

		// user_data 구분 태그:
		//   비트 63 = 1  → 소켓 POLL_ADD CQE  (MakePollUserData 인코딩)
		//   비트 63 = 0  → IoCtx* (소켓 proactor recv/send 또는 파일 read/write)
		static constexpr uint64_t kSocketPollTag = 1ULL << 63;

		io_uring ring{};
		bool_t   valid{ false };

		std::unordered_map<socket_t, WatchEntry> watches;
		uint32_t                                 nextGeneration{ 0 };

		ne::time::TimerWheel* timerWheel{ nullptr };

	public:
		[[nodiscard]] bool_t IsValid() const noexcept { return valid; }

		// ── Proactor: 파일 I/O (IIoEngine 외부 — AsyncFile 전용) ─────────────
		[[nodiscard]] ne::Result<void, ne::OsError>
			SubmitRead(int _fd, void* _buf, std::size_t _len, std::size_t _offset, IoCtx* _ctx) noexcept;

		[[nodiscard]] ne::Result<void, ne::OsError>
			SubmitWrite(int _fd, const void* _buf, std::size_t _len, std::size_t _offset, IoCtx* _ctx) noexcept;

		// ── IIoEngine<>: Reactor (POLL_ADD) ─────────────────────────────────
		[[nodiscard]] ne::Result<void, ne::OsError> Watch(socket_t _fd, uint32_t _events, IoCallback _cb) override;
		[[nodiscard]] ne::Result<void, ne::OsError> Unwatch(socket_t _fd) override;

		// ── IIoEngine<>: Proactor (소켓 RECV/SEND) ───────────────────────────
		[[nodiscard]] ne::Result<void, ne::OsError> SubmitRecv(socket_t _fd, void* _buf, std::size_t _len, IoCtx* _ctx) noexcept override;
		[[nodiscard]] ne::Result<void, ne::OsError> SubmitSend(socket_t _fd, const void* _buf, std::size_t _len, IoCtx* _ctx) noexcept override;

		// ── IIoEngine<>: 공통 ────────────────────────────────────────────────
		[[nodiscard]] ne::Result<void, ne::OsError> RunOnce(ne::int_t _timeoutMs = -1) override;
		void SetTimerWheel(ne::time::TimerWheel* _wheel) noexcept override { timerWheel = _wheel; }

	private:
		void ProcessCqe(io_uring_cqe* _cqe) noexcept;

		// user_data 인코딩: [63] kSocketPollTag | [32-62] generation | [0-31] fd
		[[nodiscard]] static uint64_t MakePollUserData(socket_t _fd, uint32_t _gen) noexcept;
		[[nodiscard]] static socket_t GetPollFd(uint64_t _userData) noexcept;
		[[nodiscard]] static uint32_t GetPollGen(uint64_t _userData) noexcept;
		[[nodiscard]] static bool_t   IsSocketPoll(uint64_t _userData) noexcept;

		static uint32_t ToPollEvents(uint32_t _events) noexcept;
		static uint32_t FromPollEvents(uint32_t _events) noexcept;
	};
END_NS

#endif // IS_POSIX
