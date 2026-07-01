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
	// 소켓 Reactor 완료 컨텍스트 — OVERLAPPED 반드시 첫 멤버 (GQCS reinterpret_cast 복원용)
	// struct SocketIocpCtx
	// {
	// 	OVERLAPPED overlapped{};
	// 	socket_t   fd{};
	// 	uint32_t   events{};
	// 	IoCallback callback;
	// };

	// Windows IOCP 기반 통합 I/O 엔진.
	//
	// Reactor (소켓 이벤트 감지):
	//   Watch  → 소켓을 IOCP 에 kReactorKey 로 연결 + zero-byte WSARecv (SocketIocpCtx)
	//   RunOnce → GQCS(key=kReactorKey) → 콜백 디스패치
	//
	// Proactor (소켓 I/O):
	//   SubmitRecv/SubmitSend → 소켓을 kIoCtxKey 로 연결 + WSARecv/WSASend (IoCtx 직접)
	//   RunOnce → GQCS(key=kIoCtxKey) → handle.resume()
	//
	// Proactor (파일 I/O):
	//   RegisterFile → 파일 HANDLE 을 kIoCtxKey 로 연결
	//   SubmitRead/Write → ReadFile/WriteFile (IoCtx 직접)
	//   RunOnce → GQCS(key=kIoCtxKey) → handle.resume()
	//
	// IoCtx.overlapped 가 첫 멤버이므로 OVERLAPPED* == reinterpret_cast<IoCtx*> 성립.
	// 소켓은 Reactor(kReactorKey) 또는 Proactor(kIoCtxKey) 중 하나로만 등록된다.
	template <ValidHandleType HandleType>
	class IocpEngine final :public IIoEngine<HandleType>
	{
	public:
		explicit IocpEngine(ulong_t _concurrentThreads = 0) noexcept
			: iocpHandle(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, _concurrentThreads)) {}
		virtual ~IocpEngine() override = default;

		NEBULA_NON_COPYABLE_MOVABLE(IocpEngine)

	private:
		using IocpHandle = ne::Handle<
			HANDLE,
			decltype([](const HANDLE _handle) { ::CloseHandle(_handle); })
		>;

		IocpHandle iocpHandle;
		std::unordered_map<socket_t, IoContext*> ioContexts; // Reactor 감시 목록
		std::unordered_set<socket_t> iocpSockets;    // IOCP 등록 소켓 추적
		ne::time::TimerWheel* timerWheel{ nullptr };

	public: // ── IIoEngine<>: Reactor ─────────────────────────────────────────
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Watch(HandleType _handle, uint32_t _events, IoCallback<HandleType> _callback) override;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Unwatch(HandleType _handle) override;

	public: // ── IIoEngine<>: Proactor (소켓 I/O) ────────────────────────────
		[[nodiscard]] virtual ne::Result<void, ne::OsError> SubmitSend(HandleType _handle, const void* _buffer, std::size_t _length, IoContext* _context) noexcept override;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> SubmitReceive(HandleType _handle, void* _buffer, std::size_t _length, IoContext* _context) noexcept override;

	public: // ── IIoEngine<>: 공통 ────────────────────────────────────────────
		[[nodiscard]] virtual ne::Result<void, ne::OsError> RunOnce(ne::int_t _timeoutMs = -1) override;
		virtual void SetTimerWheel(ne::time::TimerWheel* _wheel) noexcept override { timerWheel = _wheel; }

	// public: // ── Proactor: 파일 I/O (IIoEngine 외부 — AsyncFile 전용) ─────────
	// 	[[nodiscard]] ne::Result<void, ne::OsError> RegisterFile(HANDLE _fileHandle) noexcept;
	// 	[[nodiscard]] ne::Result<void, ne::OsError> SubmitRead(HANDLE _fd, void* _buffer, std::size_t _length, std::size_t _offset, IoContext* _ctx) noexcept;
	// 	[[nodiscard]] ne::Result<void, ne::OsError> SubmitWrite(HANDLE _fd, const void* _buffer, std::size_t _length, std::size_t _offset, IoContext* _ctx) noexcept;

	private:
		[[nodiscard]] ne::Result<void, ne::OsError> EnsureSocketInIocp(socket_t _fd, ULONG_PTR _key) noexcept;
		void ReleaseSocketContext(socket_t _fd);

	public:
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(iocpHandle); }
		[[nodiscard]] HANDLE NativeHandle() const noexcept { return iocpHandle.Get(); }
	};

END_NS

#endif // _WIN32
