//
// Created by hscloud on 26. 6. 30.
//

#if defined(_WIN32)
#include "IocpEngine.h"
#include <winsock2.h>
#include "TimerWheel.h"

BEGIN_NS(ne::io)
	IocpEngine::IocpEngine(const ulong_t _concurrentThreads) noexcept
		: iocpHandle(::CreateIoCompletionPort(
			INVALID_HANDLE_VALUE, nullptr, 0,
			static_cast<DWORD>(_concurrentThreads)))
	{}



	// ── IIoEngine: Reactor (소켓 이벤트) ─────────────────────────────────────

	ne::Result<void, ne::OsError> IocpEngine::Watch(const socket_t _fd, const uint32_t _events, IoCallback _cb)
	{
		// 이미 등록된 fd — 컨텍스트 갱신 후 zero-byte WSARecv 재제출
		if (const auto it = socketContexts.find(_fd); it != socketContexts.end())
		{
			it->second->events   = _events;
			it->second->callback = std::move(_cb);

			if (_events & IoEvent::Read)
			{
				it->second->overlapped = {};
				WSABUF wsaBuf{ .len = 0, .buf = nullptr };
				DWORD  flags = 0;
				::WSARecv(
					static_cast<SOCKET>(_fd),
					&wsaBuf, 1,
					nullptr, &flags,
					&it->second->overlapped,
					nullptr);
			}
			return ne::Result<void, ne::OsError>::Ok();
		}

		auto* ctx     = new SocketIocpCtx{};
		ctx->fd       = _fd;
		ctx->events   = _events;
		ctx->callback = std::move(_cb);

		// 소켓을 IOCP 에 연결. SocketCompletionKey 로 소켓/파일 완료를 구분.
		const HANDLE assoc = ::CreateIoCompletionPort(
			reinterpret_cast<HANDLE>(_fd),
			iocpHandle.Get(),
			SocketCompletionKey,
			0
		);
		if (!assoc)
		{
			delete ctx;
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[IocpEngine/Watch]"));
		}

		socketContexts[_fd] = ctx;

		// Read 이벤트 요청: zero-byte WSARecv 로 소켓 준비 알림 등록.
		// 실제 데이터 수신은 콜백을 받은 Stream 레이어가 담당.
		if (_events & IoEvent::Read)
		{
			WSABUF wsaBuf{ .len = 0, .buf = nullptr };
			DWORD  flags = 0;
			::WSARecv(
				static_cast<SOCKET>(_fd),
				&wsaBuf, 1,
				nullptr, &flags,
				&ctx->overlapped,
				nullptr);
			// ERROR_IO_PENDING 은 정상 경로
		}

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IocpEngine::Unwatch(const socket_t _fd)
	{
		ReleaseSocketContext(_fd);
		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IocpEngine::RunOnce(const ne::int_t _timeoutMs)
	{
		DWORD effectiveTimeout = (_timeoutMs < 0) ? INFINITE : static_cast<DWORD>(_timeoutMs);
		if (timerWheel)
		{
			const ne::int_t nextExpiry = timerWheel->NextExpiryMs();
			if (nextExpiry >= 0)
			{
				const DWORD t = static_cast<DWORD>(nextExpiry);
				if (effectiveTimeout == INFINITE || t < effectiveTimeout) effectiveTimeout = t;
			}
		}

		DWORD       bytes = 0;
		ULONG_PTR   key   = 0;
		OVERLAPPED* ov    = nullptr;

		const BOOL ok = ::GetQueuedCompletionStatus(
			iocpHandle.Get(), &bytes, &key, &ov, effectiveTimeout);

		if (!ov)
		{
			// WAIT_TIMEOUT 또는 PostQueuedCompletionStatus(nullptr) wakeup — 둘 다 정상
			if (timerWheel) timerWheel->Tick();
			if (!ok && ::GetLastError() != WAIT_TIMEOUT)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ ne::LastOsError() }.Context("[IocpEngine/RunOnce]"));
			return ne::Result<void, ne::OsError>::Ok();
		}

		if (key == SocketCompletionKey)
		{
			// 소켓 이벤트 완료
			auto* ctx = reinterpret_cast<SocketIocpCtx*>(ov);
			if (!ok)
			{
				ctx->callback(ctx->fd, IoEvent::Error | IoEvent::HangUp);
				ReleaseSocketContext(ctx->fd);
			}
			else
			{
				// WSARecv 완료 = Read 이벤트; 0 바이트 = 상대방 연결 종료
				uint32_t evts = IoEvent::Read;
				if (bytes == 0) evts |= IoEvent::HangUp;
				ctx->callback(ctx->fd, evts);
			}
		}
		else if (key == FileCompletionKey)
		{
			// 파일 I/O 완료 — RunOnce 스레드에서 handle.resume() 호출
			auto* ctx = reinterpret_cast<FileIocpCtx*>(ov);
			if (!ok)
				ctx->result = ne::Result<std::size_t, ne::OsError>::Error(
					ne::OsError{ ne::LastOsError() }.Context("[IocpEngine/RunOnce/File]"));
			else
				ctx->result = ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(bytes));

			if (ctx->handle && !ctx->handle.done())
				ctx->handle.resume();
		}

		if (timerWheel) timerWheel->Tick();
		return ne::Result<void, ne::OsError>::Ok();
	}



	// ── Proactor: 파일 I/O ────────────────────────────────────────────────────

	ne::Result<void, ne::OsError> IocpEngine::RegisterFile(const HANDLE _fileHandle) noexcept
	{
		const HANDLE assoc = ::CreateIoCompletionPort(
			_fileHandle, iocpHandle.Get(), FileCompletionKey, 0);
		if (!assoc)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[IocpEngine/RegisterFile]"));
		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IocpEngine::SubmitRead(
		const HANDLE _fd, void* _buf, const std::size_t _len, const std::size_t _offset,
		FileIocpCtx* _ctx) noexcept
	{
		_ctx->overlapped             = {};
		_ctx->overlapped.Offset     = static_cast<DWORD>(_offset & 0xFFFFFFFF);
		_ctx->overlapped.OffsetHigh = static_cast<DWORD>(_offset >> 32);

		if (!::ReadFile(_fd, _buf, static_cast<DWORD>(_len), nullptr, &_ctx->overlapped))
		{
			const DWORD err = ::GetLastError();
			if (err != ERROR_IO_PENDING)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ static_cast<ne::ulong_t>(err) }.Context("[IocpEngine/SubmitRead]"));
		}
		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IocpEngine::SubmitWrite(
		const HANDLE _fd, const void* _buf, const std::size_t _len, const std::size_t _offset,
		FileIocpCtx* _ctx) noexcept
	{
		_ctx->overlapped             = {};
		_ctx->overlapped.Offset     = static_cast<DWORD>(_offset & 0xFFFFFFFF);
		_ctx->overlapped.OffsetHigh = static_cast<DWORD>(_offset >> 32);

		if (!::WriteFile(_fd, _buf, static_cast<DWORD>(_len), nullptr, &_ctx->overlapped))
		{
			const DWORD err = ::GetLastError();
			if (err != ERROR_IO_PENDING)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ static_cast<ne::ulong_t>(err) }.Context("[IocpEngine/SubmitWrite]"));
		}
		return ne::Result<void, ne::OsError>::Ok();
	}



	void IocpEngine::ReleaseSocketContext(const socket_t _fd)
	{
		if (const auto it = socketContexts.find(_fd); it != socketContexts.end())
		{
			delete it->second;
			socketContexts.erase(it);
		}
	}
END_NS

#endif // _WIN32
