//
// Created by hscloud on 26. 6. 30.
//

#include "IocpEngine.h"

#if defined(_WIN32)
#include <winsock2.h>
#include "Timer/TimerWheel.h"



BEGIN_NS(ne::io)
	IocpEngine::IocpEngine(const ulong_t _concurrentThreads) noexcept
		: iocpHandle(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, _concurrentThreads)) {}



	ne::Result<void, ne::OsError> IocpEngine::EnsureSocketInIocp(const socket_t _fd, const ULONG_PTR _key) noexcept
	{
		if (iocpSockets.contains(_fd)) return ne::Result<void, ne::OsError>::Ok();

		const HANDLE associate = ::CreateIoCompletionPort(
			reinterpret_cast<HANDLE>(_fd), iocpHandle.Get(), _key, 0);

		if (!associate)
		{
			const ulong_t err = ::GetLastError();
			// ERROR_INVALID_PARAMETER: 이미 이 IOCP 에 연결된 소켓 — 정상으로 처리
			if (err != ERROR_INVALID_PARAMETER)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ err }.Context("[IocpEngine/EnsureSocketInIocp]"));
		}

		iocpSockets.insert(_fd);
		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IocpEngine::Watch(const socket_t _fd, const uint32_t _events, IoCallback _callback)
	{
		// 이미 등록된 fd — 컨텍스트 갱신 후 zero-byte WSARecv 재제출
		if (const auto it = socketContexts.find(_fd); it != socketContexts.end())
		{
			it->second->events   = _events;
			it->second->callback = std::move(_callback);

			if (_events & IoEvent::Read)
			{
				it->second->overlapped = {};
				WSABUF wsaBuf{ .len = 0, .buf = nullptr };
				ulong_t flags = 0;
				::WSARecv(_fd, &wsaBuf, 1, nullptr, &flags, &it->second->overlapped, nullptr);
			}
			return ne::Result<void, ne::OsError>::Ok();
		}

		// 새 fd — kReactorKey 로 IOCP 연결 후 Reactor 컨텍스트 생성
		if (auto r = EnsureSocketInIocp(_fd, kReactorKey); r.IsError()) return r;

		auto* ctx     = new SocketIocpCtx{};
		ctx->fd       = _fd;
		ctx->events   = _events;
		ctx->callback = std::move(_callback);

		socketContexts[_fd] = ctx;

		// Read 이벤트: zero-byte WSARecv 로 소켓 준비 알림 등록
		if (_events & IoEvent::Read)
		{
			WSABUF wsaBuf{ .len = 0, .buf = nullptr };
			ulong_t flags = 0;
			::WSARecv(_fd, &wsaBuf, 1, nullptr, &flags, &ctx->overlapped, nullptr);
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
		ulong_t effectiveTimeout = (_timeoutMs < 0) ? INFINITE : static_cast<ulong_t>(_timeoutMs);
		if (timerWheel)
		{
			const ne::int_t nextExpiry = timerWheel->NextExpiryMs();
			if (nextExpiry >= 0)
			{
				const ulong_t t = static_cast<ulong_t>(nextExpiry);
				if (effectiveTimeout == INFINITE || t < effectiveTimeout)
					effectiveTimeout = t;
			}
		}

		ulong_t    bytes      = 0;
		ULONG_PTR  key        = 0;
		OVERLAPPED* overlapped = nullptr;

		const BOOL ok = ::GetQueuedCompletionStatus(iocpHandle.Get(), &bytes, &key, &overlapped, effectiveTimeout);
		if (!overlapped)
		{
			if (timerWheel) timerWheel->Tick();
			if (!ok && ::GetLastError() != WAIT_TIMEOUT)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ ne::LastOsError() }.Context("[IocpEngine/RunOnce]"));
			return ne::Result<void, ne::OsError>::Ok();
		}

		if (key == kReactorKey)
		{
			// Reactor 경로: zero-byte WSARecv 완료 → 콜백 디스패치
			auto* ctx = reinterpret_cast<SocketIocpCtx*>(overlapped);
			if (!ok)
			{
				ctx->callback(ctx->fd, IoEvent::Error | IoEvent::HangUp);
				ReleaseSocketContext(ctx->fd);
			}
			else
			{
				uint32_t events = IoEvent::Read;
				if (bytes == 0) events |= IoEvent::HangUp;
				ctx->callback(ctx->fd, events);
			}
		}
		else // kIoCtxKey — 소켓 proactor 또는 파일 proactor
		{
			// IoCtx::overlapped 가 첫 멤버이므로 overlapped == reinterpret_cast<OVERLAPPED*>(ctx)
			auto* ctx = reinterpret_cast<IoContext*>(overlapped);
			if (!ok)
				ctx->result = ne::Result<std::size_t, ne::OsError>::Error(
					ne::OsError{ ne::LastOsError() }.Context("[IocpEngine/RunOnce]"));
			else
				ctx->result = ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(bytes));

			if (ctx->handle && !ctx->handle.done())
				ctx->handle.resume();
		}

		if (timerWheel) timerWheel->Tick();
		return ne::Result<void, ne::OsError>::Ok();
	}



	ne::Result<void, ne::OsError> IocpEngine::SubmitRecv(
		const socket_t _fd, void* _buf, const std::size_t _len, IoContext* _ctx) noexcept
	{
		if (auto r = EnsureSocketInIocp(_fd, kIoCtxKey); r.IsError()) return r;

		_ctx->overlapped = {};
		WSABUF wsaBuf{ .len = static_cast<ulong_t>(_len), .buf = static_cast<char*>(_buf) };
		ulong_t recvd = 0, flags = 0;

		if (::WSARecv(static_cast<SOCKET>(_fd), &wsaBuf, 1, &recvd, &flags, &_ctx->overlapped, nullptr) == SOCKET_ERROR)
		{
			if (const int wsa = ::WSAGetLastError(); wsa != WSA_IO_PENDING)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ static_cast<ne::ulong_t>(wsa) }.Context("[IocpEngine/SubmitRecv]"));
		}

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IocpEngine::SubmitSend(
		const socket_t _fd, const void* _buf, const std::size_t _len, IoContext* _ctx) noexcept
	{
		if (auto r = EnsureSocketInIocp(_fd, kIoCtxKey); r.IsError()) return r;

		_ctx->overlapped = {};
		WSABUF wsaBuf{ .len = static_cast<ulong_t>(_len), .buf = const_cast<char*>(static_cast<const char*>(_buf)) };
		ulong_t sent = 0;

		if (::WSASend(static_cast<SOCKET>(_fd), &wsaBuf, 1, &sent, 0, &_ctx->overlapped, nullptr) == SOCKET_ERROR)
		{
			if (const int wsa = ::WSAGetLastError(); wsa != WSA_IO_PENDING)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ static_cast<ne::ulong_t>(wsa) }.Context("[IocpEngine/SubmitSend]"));
		}

		return ne::Result<void, ne::OsError>::Ok();
	}



	ne::Result<void, ne::OsError> IocpEngine::RegisterFile(const HANDLE _fileHandle) noexcept
	{
		const HANDLE associate = ::CreateIoCompletionPort(_fileHandle, iocpHandle.Get(), kIoCtxKey, 0);
		if (!associate)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[IocpEngine/RegisterFile]"));
		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IocpEngine::SubmitRead(
		const HANDLE _fd, void* _buffer, const std::size_t _length, const std::size_t _offset, IoContext* _ctx) noexcept
	{
		_ctx->overlapped        = {};
		_ctx->overlapped.Offset     = static_cast<ulong_t>(_offset & 0xFFFFFFFF);
		_ctx->overlapped.OffsetHigh = static_cast<ulong_t>(_offset >> 32);

		if (!::ReadFile(_fd, _buffer, static_cast<ulong_t>(_length), nullptr, &_ctx->overlapped))
		{
			if (const ulong_t err = ::GetLastError(); err != ERROR_IO_PENDING)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ err }.Context("[IocpEngine/SubmitRead]"));
		}
		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IocpEngine::SubmitWrite(
		const HANDLE _fd, const void* _buffer, const std::size_t _length, const std::size_t _offset, IoContext* _ctx) noexcept
	{
		_ctx->overlapped        = {};
		_ctx->overlapped.Offset     = static_cast<ulong_t>(_offset & 0xFFFFFFFF);
		_ctx->overlapped.OffsetHigh = static_cast<ulong_t>(_offset >> 32);

		if (!::WriteFile(_fd, _buffer, static_cast<ulong_t>(_length), nullptr, &_ctx->overlapped))
		{
			if (const ulong_t err = ::GetLastError(); err != ERROR_IO_PENDING)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ err }.Context("[IocpEngine/SubmitWrite]"));
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
