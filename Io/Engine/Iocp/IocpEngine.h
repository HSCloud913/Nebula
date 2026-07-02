//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#if defined(_WIN32)

#include "Type.h"
#include <unordered_map>
#include <unordered_set>
#include <coroutine>
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
		std::unordered_set<socket_t> iocpSockets;    // IOCP 등록 소켓 추적
		std::unordered_map<socket_t, WatchEntry> watches; // Reactor 감시 목록
		ne::time::TimerWheel* timerWheel{ nullptr };

	public: // Socket Reactor
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Watch(socket_t _fd, uint32_t _events, IoCallback _callback) override;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Unwatch(socket_t _fd) override;

	public: // Socket Proactor
		[[nodiscard]] virtual ne::Result<void, ne::OsError> SubmitSend(socket_t _fd, const void* _buffer, std::size_t _length, IoContext* _context) noexcept override;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> SubmitReceive(socket_t _fd, void* _buffer, std::size_t _length, IoContext* _context) noexcept override;

	public: // Common
		[[nodiscard]] virtual ne::Result<void, ne::OsError> RunOnce(ne::int_t _timeoutMs = -1) override;
		virtual void SetTimerWheel(ne::time::TimerWheel* _wheel) noexcept override { timerWheel = _wheel; }

	private: // Socket 전용
		[[nodiscard]] ne::Result<void, ne::OsError> EnsureSocketInIocp(socket_t _fd, ULONG_PTR _key) noexcept;
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
