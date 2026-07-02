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

BEGIN_NS (ne::io)
	// Linux io_uring 단일 경로 통합 I/O 엔진.
	//
	// 소켓 Reactor (IIoEngine — POLL_ADD):
	//   Watch   → IORING_OP_POLL_ADD SQE 제출 (one-shot)
	//   Unwatch → IORING_OP_POLL_REMOVE SQE 제출 (best-effort)
	//
	// 소켓 Proactor (IIoEngine — RECV/SEND):
	//   SubmitReceive/SubmitSend → IORING_OP_RECV/SEND, user_data = IoContext*
	//
	// 파일 Proactor (IIoEngine 외부 — AsyncFile 전용):
	//   SubmitRead/SubmitWrite → IORING_OP_READ/WRITE, user_data = IoContext*
	//
	// RunOnce: io_uring_wait_cqe_timeout → io_uring_peek_batch_cqe → CQE 분류:
	//   user_data bit63=1 → 소켓 POLL_ADD CQE → watches 맵 조회 후 콜백 디스패치
	//   user_data bit63=0 → IoContext* (소켓 proactor or 파일) → result 저장 → handle.resume()
	//
	// x86-64 사용자 공간 주소는 최대 2^47 이므로 비트 47 이상은 항상 0.
	//
	// fd 하나에 Read 방향과 Write 방향을 동시에 watch 할 수 있다 — WatchSlots 가 두 방향을
	// 독립된 WatchEntry(+ 독립된 generation) 로 보관하고, POLL_ADD 도 방향별로 별도 제출한다
	// (user_data bit62 로 방향 구분). 한 fd 에 POLL_ADD 를 POLLIN|POLLOUT 합쳐 하나만 걸면,
	// 한쪽이 먼저 fire 했을 때 CQE 하나로 poll 등록 전체가 소모되어(io_uring POLL_ADD 는
	// one-shot) 반대 방향의 감시까지 암묵적으로 사라지는 문제가 있어 이렇게 분리한다.
	class IoUringEngine final :public IIoEngine
	{
	public:
		explicit IoUringEngine(uint_t _queueDepth = 256) noexcept;
		virtual ~IoUringEngine() override;

		NEBULA_NON_COPYABLE_MOVABLE(IoUringEngine)

	private: // CQE 의 user_data 하나로 소켓 poll 완료와 소켓·파일 proactor 완료를 구분.
		//   비트 63 = 1 → 소켓 POLL_ADD CQE  (MakePollUserData 인코딩)
		//   비트 63 = 0 → IoContext* (소켓 proactor recv/send 또는 파일 read/write)
		//   비트 62      → (비트 63=1 일 때만 의미) 0=Read 슬롯, 1=Write 슬롯
		static constexpr uint64_t SocketPollTag = 1ULL << 63;
		static constexpr uint64_t WriteDirTag = 1ULL << 62;
		static constexpr uint_t MaxCqeBatch = 64;

	private:
		io_uring ring{};
		std::unordered_map<socket_t, WatchSlots> watches; // fd 별 Read/Write 방향 독립 감시
		uint32_t nextGeneration{ 0 };
		bool_t isValid{ false };
		ne::time::TimerWheel* timerWheel{ nullptr };

	public: // Socket Reactor
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Watch(socket_t _fd, uint32_t _events, IoCallback _callback) override;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Unwatch(socket_t _fd, uint32_t _events = IoEvent::Read | IoEvent::Write) override;

	public: // Socket Proactor
		[[nodiscard]] virtual ne::Result<void, ne::OsError> SubmitSend(socket_t _fd, const void* _buffer, std::size_t _length, IoContext* _context) noexcept override;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> SubmitReceive(socket_t _fd, void* _buffer, std::size_t _length, IoContext* _context) noexcept override;

	public: // Common
		[[nodiscard]] virtual ne::Result<void, ne::OsError> RunOnce(ne::int_t _timeoutMs = -1) override;
		virtual void SetTimerWheel(ne::time::TimerWheel* _wheel) noexcept override { timerWheel = _wheel; }

	public: // File 전용 (Proactor, IIoEngine 외부 — AsyncFile 전용)
		[[nodiscard]] ne::Result<void, ne::OsError> SubmitRead(int _fd, void* _buffer, std::size_t _length, std::size_t _offset, IoContext* _context) noexcept;
		[[nodiscard]] ne::Result<void, ne::OsError> SubmitWrite(int _fd, const void* _buffer, std::size_t _length, std::size_t _offset, IoContext* _context) noexcept;

	private: // 내부 구현
		void ProcessCqe(io_uring_cqe* _cqe) noexcept;

		// user_data 인코딩: [63] kSocketPollTag | [62] kWriteDirTag | [32-61] generation(30bit) | [0-31] fd
		[[nodiscard]] static uint64_t MakePollUserData(socket_t _fd, bool_t _isWrite, uint32_t _gen) noexcept;
		[[nodiscard]] static socket_t GetPollFd(uint64_t _userData) noexcept { return static_cast<socket_t>(static_cast<uint32_t>(_userData & 0xFFFF'FFFFu)); }
		[[nodiscard]] static uint32_t GetPollGen(uint64_t _userData) noexcept { return static_cast<uint32_t>((_userData >> 32) & 0x3FFF'FFFFu); }
		[[nodiscard]] static bool_t IsSocketPoll(uint64_t _userData) noexcept { return (_userData & SocketPollTag) != 0; }
		[[nodiscard]] static bool_t IsWriteDir(uint64_t _userData) noexcept { return (_userData & WriteDirTag) != 0; }

		static uint32_t ToPollEvents(uint32_t _events) noexcept;
		static uint32_t FromPollEvents(uint32_t _events) noexcept;

	public:
		[[nodiscard]] bool_t IsValid() const noexcept { return isValid; }
	};

END_NS

#endif // IS_POSIX
