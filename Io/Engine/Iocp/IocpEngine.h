//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#if defined(_WIN32)

#include <unordered_map>
#include <coroutine>
#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include "Engine/IIoEngine.h"
#include "Handle.h"
#include "Result.h"
#include "Error.h"
#include "Type.h"

BEGIN_NS(ne::io)
	// 소켓 완료 컨텍스트 — OVERLAPPED 반드시 첫 멤버 (reinterpret_cast 복원용)
	struct SocketIocpCtx
	{
		OVERLAPPED overlapped{};
		socket_t   fd{};
		uint32_t   events{};
		IoCallback callback;
	};

	// 파일 완료 컨텍스트 — OVERLAPPED 반드시 첫 멤버 (reinterpret_cast 복원용)
	struct FileIocpCtx
	{
		OVERLAPPED              overlapped{};
		std::coroutine_handle<> handle;
		ne::Result<std::size_t, ne::OsError> result{ ne::Result<std::size_t, ne::OsError>::Ok(0) };
	};

	// Windows IOCP 기반 통합 I/O 엔진.
	//
	// 소켓 이벤트 (IIoEngine — reactor):
	//   Watch  → 소켓을 IOCP 에 연결 + zero-byte WSARecv 제출
	//   RunOnce → GQCS → SocketCompletionKey 경로 → 콜백 디스패치
	//
	// 파일 I/O (proactor):
	//   RegisterFile → 파일 HANDLE 을 IOCP 에 연결
	//   SubmitRead/Write → ReadFile/WriteFile + OVERLAPPED
	//   RunOnce → GQCS → FileCompletionKey 경로 → handle.resume()
	//
	// 모든 코루틴 resume 은 RunOnce() 호출 스레드에서 발생함이 보장된다.
	class IocpEngine final : public IIoEngine
	{
		NEBULA_NON_COPYABLE_MOVABLE(IocpEngine)

	public:
		// concurrentThreads: 0 이면 OS 가 논리 프로세서 수로 자동 결정
		explicit IocpEngine(ulong_t _concurrentThreads = 0) noexcept;
		~IocpEngine() override = default;

	private:
		static constexpr ULONG_PTR SocketCompletionKey = 1;
		static constexpr ULONG_PTR FileCompletionKey   = 2;

		using IocpHandle = ne::Handle<
			HANDLE,
			decltype([](const HANDLE _h) { ::CloseHandle(_h); })
		>;

		IocpHandle iocpHandle;
		std::unordered_map<socket_t, SocketIocpCtx*> socketContexts;
		ne::time::TimerWheel* timerWheel{ nullptr };

	public:
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(iocpHandle); }
		[[nodiscard]] HANDLE NativeHandle() const noexcept { return iocpHandle.Get(); }

		// ── IIoEngine: Reactor (소켓 이벤트) ─────────────────────────────────
		[[nodiscard]] ne::Result<void, ne::OsError> Watch(socket_t _fd, uint32_t _events, IoCallback _cb) override;
		[[nodiscard]] ne::Result<void, ne::OsError> Unwatch(socket_t _fd) override;
		[[nodiscard]] ne::Result<void, ne::OsError> RunOnce(ne::int_t _timeoutMs = -1) override;
		void SetTimerWheel(ne::time::TimerWheel* _wheel) noexcept override { timerWheel = _wheel; }

		// ── Proactor: 파일 I/O ────────────────────────────────────────────────
		// AsyncFile::Create/Open 에서 파일 핸들을 이 IOCP 에 등록
		[[nodiscard]] ne::Result<void, ne::OsError> RegisterFile(HANDLE _fileHandle) noexcept;

		[[nodiscard]] ne::Result<void, ne::OsError>
			SubmitRead(HANDLE _fd, void* _buf, std::size_t _len, std::size_t _offset, FileIocpCtx* _ctx) noexcept;

		[[nodiscard]] ne::Result<void, ne::OsError>
			SubmitWrite(HANDLE _fd, const void* _buf, std::size_t _len, std::size_t _offset, FileIocpCtx* _ctx) noexcept;

	private:
		void ReleaseSocketContext(socket_t _fd);
	};
END_NS

#endif // _WIN32
