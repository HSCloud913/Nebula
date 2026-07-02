//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#if defined(_WIN32)

#include "Type.h"
#include <mutex>
#include <unordered_map>
#include <windows.h>
#include "Engine/IIoEngine.h"
#include "Handle.h"
#include "Result.h"
#include "Error.h"

BEGIN_NS(ne::io)
	// Windows IOCP 기반 통합 I/O 엔진.
	//
	// Reactor (소켓 이벤트 감지):
	//   Watch  → 소켓을 IOCP 에 ReactorKey 로 연결 + zero-byte WSARecv/WSASend (WatchEntry, watches 맵 값으로 직접 저장)
	//   RunOnce → GQCS(key=ReactorKey) → 콜백 디스패치
	//
	// Proactor (소켓 I/O):
	//   SubmitReceive/SubmitSend → 소켓을 ProactorKey 로 연결 + WSARecv/WSASend (IoContext 직접)
	//   RunOnce → GQCS(key=ProactorKey) → handle.resume()
	//
	// Proactor (파일 I/O, IIoEngine 외부 — AsyncFile 전용):
	//   RegisterFile → 파일 HANDLE 을 ProactorKey 로 연결
	//   SubmitRead/Write → ReadFile/WriteFile (IoContext 직접)
	//   RunOnce → GQCS(key=ProactorKey) → handle.resume()
	//
	// WatchEntry/IoContext 모두 OVERLAPPED 가 첫 멤버이므로 OVERLAPPED* 로부터 reinterpret_cast 복원 성립.
	// 소켓은 Reactor(ReactorKey) 또는 Proactor(ProactorKey) 중 하나로만 등록된다 —
	// 같은 소켓을 두 키로 동시에 등록할 수 없다(CreateIoCompletionPort 는 핸들당 최초 1회만 연결 가능).
	// iocpSockets 가 fd 별로 등록된 key 를 기억해 두므로, 이미 다른 key 로 등록된 소켓에
	// Watch 와 SubmitSend/SubmitReceive 를 섞어 쓰면(EnsureSocketInIocp) 조용히 무시되지 않고
	// 에러로 거부된다 — 그렇지 않으면 완료가 엉뚱한 GQCS 분기로 들어가 OVERLAPPED* 를 잘못된
	// 타입(WatchEntry* ↔ IoContext*)으로 reinterpret_cast 하게 되어 메모리가 오염된다.
	//
	// watches 는 fd 하나당 WatchSlots(read/write 슬롯 2개)를 보관해 Read 방향과 Write 방향을
	// 서로 독립적으로 Watch/Unwatch 할 수 있다(예: 한 코루틴이 recv 대기 중에 다른 코루틴이
	// 같은 fd 로 send 를 대기). 두 슬롯 모두 비활성이고 어느 쪽도 zero-byte I/O 가 커널에
	// pending 상태가 아닐 때만 fd 전체(iocpSockets 포함)를 정리한다.
	//
	// _concurrentThreads(생성자)는 IOCP 의 표준 멀티스레드 워커 모델 그대로 여러 스레드가 같은
	// IocpEngine 에서 동시에 RunOnce() 를 호출하는 것을 전제한다 — mutex 로 watches/iocpSockets
	// 접근을 보호한다. GetQueuedCompletionStatus 의 블로킹 대기 자체와 콜백/handle.resume() 호출은
	// 반드시 락 밖에서 수행한다 — 콜백이 동기적으로 Watch()/Unwatch() 를 재호출하는 경우가 실제로
	// 있어(RecvAwaitable 등) 락을 쥔 채 호출하면 재진입 데드락이 된다.
	class IocpEngine final :public IIoEngine
	{
	public:
		explicit IocpEngine(ulong_t _concurrentThreads = 0) noexcept;
		virtual ~IocpEngine() override = default;

		NEBULA_NON_COPYABLE_MOVABLE(IocpEngine)

	private: // GetQueuedCompletionStatus 의 completionKey — Reactor 완료와 Proactor(소켓+파일) 완료를 구분.
		static constexpr ULONG_PTR ReactorKey = 1;
		static constexpr ULONG_PTR ProactorKey = 2;

	private:
		using IocpHandle = ne::Handle<HANDLE, decltype([](const HANDLE _handle) { ::CloseHandle(_handle); })>;

		IocpHandle iocpHandle;
		std::mutex mutex; // watches/iocpSockets 보호(멀티스레드 RunOnce) — 콜백 호출 중에는 절대 보유하지 않는다.
		std::unordered_map<socket_t, ULONG_PTR> iocpSockets; // IOCP 등록 소켓 → 등록된 completion key
		std::unordered_map<socket_t, WatchSlots> watches; // fd 별 Read/Write 방향 독립 감시
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

	private: // Socket 전용 — 모두 mutex 를 이미 쥐고 있다고 가정한다(재진입 락을 걸지 않음).
		[[nodiscard]] ne::Result<void, ne::OsError> EnsureSocketInIocp(socket_t _fd, ULONG_PTR _key) noexcept;
		[[nodiscard]] ne::Result<void, ne::OsError> ArmWatch(WatchEntry& _entry) noexcept;
		void ReleaseSlot(WatchEntry& _entry, socket_t _fd) noexcept;
		void ReleaseSocketContextIfIdle(socket_t _fd) noexcept;
		void ReleaseSocketContext(socket_t _fd);

	public: // File 전용 (Proactor)
		[[nodiscard]] ne::Result<void, ne::OsError> RegisterFileHandle(HANDLE _handle) noexcept;
		[[nodiscard]] ne::Result<void, ne::OsError> SubmitRead(HANDLE _handle, void* _buffer, std::size_t _length, std::size_t _offset, IoContext* _context) noexcept;
		[[nodiscard]] ne::Result<void, ne::OsError> SubmitWrite(HANDLE _handle, const void* _buffer, std::size_t _length, std::size_t _offset, IoContext* _context) noexcept;

	public:
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(iocpHandle); }
		[[nodiscard]] HANDLE NativeHandle() const noexcept { return iocpHandle.Get(); }
	};

END_NS

#endif // _WIN32
