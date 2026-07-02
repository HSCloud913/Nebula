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



	ne::Result<void, ne::OsError> IocpEngine::Watch(const socket_t _fd, const uint32_t _events, IoCallback _callback)
	{
		// 이미 등록된 fd — 컨텍스트 갱신 후 zero-byte WSARecv/WSASend 재제출
		if (const auto watch = watches.find(_fd); watch != watches.end())
		{
			watch->second.events = _events;
			watch->second.callback = std::move(_callback);
			watch->second.overlapped = {};

			if (_events & IoEvent::Read)
			{
				WSABUF wsaBuffer{ .len = 0, .buf = nullptr };
				ulong_t flags = 0;
				::WSARecv(_fd, &wsaBuffer, 1, nullptr, &flags, &watch->second.overlapped, nullptr);
			}
			else if (_events & IoEvent::Write)
			{
				WSABUF wsaBuffer{ .len = 0, .buf = nullptr };
				::WSASend(_fd, &wsaBuffer, 1, nullptr, 0, &watch->second.overlapped, nullptr);
			}

			return ne::Result<void, ne::OsError>::Ok();
		}

		// 새 fd — ReactorKey 로 IOCP 연결 후 Reactor 컨텍스트 생성.
		// watches 맵에 값으로 직접 저장 — unordered_map 은 erase 전까지 원소의 참조/주소를
		// 보장하므로 힙 할당 없이 &entry.overlapped 를 OS 에 안전하게 넘길 수 있다.
		if (auto result = EnsureSocketInIocp(_fd, ReactorKey); result.IsError()) return result;

		auto& [overlapped, fd, events, callback] = watches[_fd];
		fd = _fd;
		events = _events;
		callback = std::move(_callback);

		// Read 이벤트: zero-byte WSARecv 로 소켓 준비 알림 등록.
		// Write 이벤트: zero-byte WSASend 로 근사 — IOCP 는 epoll 의 EPOLLOUT 처럼 "쓰기 가능"
		// 자체를 알리는 완료 통지가 없으므로, 빈 송신 완료를 준비완료 신호로 대신 사용한다
		// (송신 버퍼가 가득 찬 드문 경우가 아니면 즉시 완료된다).
		if (_events & IoEvent::Read)
		{
			WSABUF wsaBuffer{ .len = 0, .buf = nullptr };
			ulong_t flags = 0;
			::WSARecv(_fd, &wsaBuffer, 1, nullptr, &flags, &overlapped, nullptr);
		}
		else if (_events & IoEvent::Write)
		{
			WSABUF wsaBuffer{ .len = 0, .buf = nullptr };
			::WSASend(_fd, &wsaBuffer, 1, nullptr, 0, &overlapped, nullptr);
		}

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IocpEngine::Unwatch(const socket_t _fd)
	{
		ReleaseSocketContext(_fd);
		return ne::Result<void, ne::OsError>::Ok();
	}



	ne::Result<void, ne::OsError> IocpEngine::SubmitSend(const socket_t _fd, const void* _buffer, const std::size_t _length, IoContext* _context) noexcept
	{
		if (auto result = EnsureSocketInIocp(_fd, ProactorKey); result.IsError()) return result;

		_context->overlapped = {};

		WSABUF wsaBuffer{ .len = static_cast<ulong_t>(_length), .buf = const_cast<char*>(static_cast<const char*>(_buffer)) };
		ulong_t sent = 0;

		if (::WSASend(_fd, &wsaBuffer, 1, &sent, 0, &_context->overlapped, nullptr) == SOCKET_ERROR)
		{
			if (const int error = ::WSAGetLastError(); error != WSA_IO_PENDING)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ static_cast<ne::ulong_t>(error) }.Context("[IocpEngine/SubmitSend]"));
		}

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IocpEngine::SubmitReceive(const socket_t _fd, void* _buffer, const std::size_t _length, IoContext* _context) noexcept
	{
		if (auto result = EnsureSocketInIocp(_fd, ProactorKey); result.IsError()) return result;

		_context->overlapped = {};

		WSABUF wsaBuffer{ .len = static_cast<ulong_t>(_length), .buf = static_cast<char*>(_buffer) };
		ulong_t received = 0, flags = 0;

		if (::WSARecv(_fd, &wsaBuffer, 1, &received, &flags, &_context->overlapped, nullptr) == SOCKET_ERROR)
		{
			if (const int error = ::WSAGetLastError(); error != WSA_IO_PENDING)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ static_cast<ne::ulong_t>(error) }.Context("[IocpEngine/SubmitReceive]"));
		}

		return ne::Result<void, ne::OsError>::Ok();
	}



	ne::Result<void, ne::OsError> IocpEngine::RunOnce(const ne::int_t _timeoutMs)
	{
		ulong_t effectiveTimeout = (_timeoutMs < 0) ? INFINITE : static_cast<ulong_t>(_timeoutMs);
		if (timerWheel)
		{
			if (const ne::int_t nextExpiry = timerWheel->NextExpiryMs(); nextExpiry >= 0)
			{
				const ulong_t timeout = static_cast<ulong_t>(nextExpiry);
				if (effectiveTimeout == INFINITE || timeout < effectiveTimeout) effectiveTimeout = timeout;
			}
		}

		ulong_t bytes = 0;
		ULONG_PTR key = 0;
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

		if (key == ReactorKey)
		{
			// Reactor 경로: zero-byte WSARecv/WSASend 완료 → 콜백 디스패치.
			// 주의: zero-byte 수신은 0바이트 전송으로 완료되므로 bytes==0 으로는
			// "데이터 도착"과 "상대가 정상 종료"를 구분할 수 없다 — 진짜 HangUp 감지가
			// 필요하면 호출자가 콜백 안에서 실제 recv 를 수행해 0 반환 여부로 판별해야 한다.
			const auto* watchEntry = reinterpret_cast<WatchEntry*>(overlapped);
			if (!ok)
			{
				watchEntry->callback(watchEntry->fd, IoEvent::Error | IoEvent::HangUp);
				ReleaseSocketContext(watchEntry->fd);
			}
			else
			{
				const uint32_t events = (watchEntry->events & IoEvent::Write) ? IoEvent::Write : IoEvent::Read;
				watchEntry->callback(watchEntry->fd, events);
			}
		}
		else // ProactorKey — 소켓 proactor 또는 파일 proactor
		{
			// IoContext::overlapped 가 첫 멤버이므로 overlapped == reinterpret_cast<OVERLAPPED*>(ctx)
			auto* context = reinterpret_cast<IoContext*>(overlapped);
			if (!ok)
			{
				context->result = ne::Result<std::size_t, ne::OsError>::Error(
					ne::OsError{ ne::LastOsError() }.Context("[IocpEngine/RunOnce]"));
			}
			else
			{
				context->result = ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(bytes));
			}

			if (context->handle && !context->handle.done()) context->handle.resume();
		}

		if (timerWheel) timerWheel->Tick();

		return ne::Result<void, ne::OsError>::Ok();
	}



	ne::Result<void, ne::OsError> IocpEngine::EnsureSocketInIocp(const socket_t _fd, const ULONG_PTR _key) noexcept
	{
		if (iocpSockets.contains(_fd)) return ne::Result<void, ne::OsError>::Ok();

		const HANDLE associate = ::CreateIoCompletionPort(reinterpret_cast<HANDLE>(_fd), iocpHandle.Get(), _key, 0);
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

	void IocpEngine::ReleaseSocketContext(const socket_t _fd)
	{
		if (const auto watch = watches.find(_fd); watch != watches.end())
		{
			watches.erase(watch);
		}
	}


	ne::Result<void, ne::OsError> IocpEngine::RegisterFileHandle(const HANDLE _handle) noexcept
	{
		const HANDLE associate = ::CreateIoCompletionPort(_handle, iocpHandle.Get(), ProactorKey, 0);
		if (!associate)
			return ne::Result<void, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[IocpEngine/RegisterFile]"));

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IocpEngine::SubmitRead(const HANDLE _handle, void* _buffer, const std::size_t _length, const std::size_t _offset, IoContext* _context) noexcept
	{
		_context->overlapped = { .Offset = static_cast<ulong_t>(_offset & 0xFFFFFFFF), .OffsetHigh = static_cast<ulong_t>(_offset >> 32) };

		if (!::ReadFile(_handle, _buffer, static_cast<ulong_t>(_length), nullptr, &_context->overlapped))
		{
			if (const ulong_t error = ::GetLastError(); error != ERROR_IO_PENDING)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ error }.Context("[IocpEngine/SubmitRead]"));
		}

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IocpEngine::SubmitWrite(const HANDLE _fd, const void* _buffer, const std::size_t _length, const std::size_t _offset, IoContext* _context) noexcept
	{
		_context->overlapped = { .Offset = static_cast<ulong_t>(_offset & 0xFFFFFFFF), .OffsetHigh = static_cast<ulong_t>(_offset >> 32) };

		if (!::WriteFile(_fd, _buffer, static_cast<ulong_t>(_length), nullptr, &_context->overlapped))
		{
			if (const ulong_t error = ::GetLastError(); error != ERROR_IO_PENDING)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ error }.Context("[IocpEngine/SubmitWrite]"));
		}

		return ne::Result<void, ne::OsError>::Ok();
	}

END_NS

#endif // _WIN32
