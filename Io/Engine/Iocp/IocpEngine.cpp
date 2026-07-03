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
		std::lock_guard lock(mutex);

		// 이미 등록된 fd — 두 방향 중 이 이벤트가 가리키는 슬롯만 갱신한다(반대 방향은 그대로).
		if (const auto watch = watches.find(_fd); watch != watches.end())
		{
			WatchEntry& entry = watch->second.Slot(_events);
			// 반대 방향은 이미 활성화됐어도 이 슬롯은 처음 쓰이는 것일 수 있다(fd 미설정) —
			// 매번 설정해도 무해하므로 무조건 갱신한다.
			entry.fd = _fd;
			entry.events = _events;
			entry.callback = std::move(_callback);

			if (entry.isPending)
			{
				// 이전 zero-byte WSARecv/WSASend 가 아직 커널에 남아 overlapped 를 참조 중 —
				// 지금 재사용하면 메모리 오염이므로 취소만 요청한다. 실제 재무장(ArmWatch)은
				// 그 완료(또는 취소 완료)가 RunOnce 로 돌아온 뒤 처리한다.
				entry.pendingAction = WatchEntry::PendingAction::REARM;
				::CancelIoEx(reinterpret_cast<HANDLE>(_fd), &entry.overlapped);
				return ne::Result<void, ne::OsError>::Ok();
			}

			entry.pendingAction = WatchEntry::PendingAction::NONE;
			return ArmWatch(entry);
		}

		// 새 fd — ReactorKey 로 IOCP 연결 후 Reactor 컨텍스트 생성.
		// watches 맵에 값으로 직접 저장 — unordered_map 은 erase 전까지 원소의 참조/주소를
		// 보장하므로 힙 할당 없이 &entry.overlapped 를 OS 에 안전하게 넘길 수 있다.
		if (auto result = EnsureSocketInIocp(_fd, ReactorKey); result.IsError()) return result;

		WatchSlots& slots = watches[_fd];
		WatchEntry& entry = slots.Slot(_events);
		entry.fd = _fd;
		entry.events = _events;
		entry.callback = std::move(_callback);

		return ArmWatch(entry);
	}

	ne::Result<void, ne::OsError> IocpEngine::Unwatch(const socket_t _fd, const uint32_t _events)
	{
		std::lock_guard lock(mutex);

		const auto watch = watches.find(_fd);
		if (watch == watches.end()) return ne::Result<void, ne::OsError>::Ok();

		if (_events & IoEvent::Read)  ReleaseSlot(watch->second.read, _fd);
		if (_events & IoEvent::Write) ReleaseSlot(watch->second.write, _fd);

		ReleaseSocketContextIfIdle(_fd);
		return ne::Result<void, ne::OsError>::Ok();
	}

	// 슬롯 하나를 비활성화한다. pending 중이면(zero-byte 제출이 아직 커널에 남아 overlapped 를
	// 참조 중) 지금 지우면 나중에 그 완료가 돌아올 때 GQCS 가 해제된 메모리를 가리키게 된다
	// (use-after-free) — 취소만 요청하고 실제 해제는 완료가 RunOnce 로 돌아온 뒤 처리한다.
	void IocpEngine::ReleaseSlot(WatchEntry& _entry, const socket_t _fd) noexcept
	{
		if (!_entry.callback) return; // 이미 비활성

		if (_entry.isPending)
		{
			_entry.pendingAction = WatchEntry::PendingAction::RELEASE;
			::CancelIoEx(reinterpret_cast<HANDLE>(_fd), &_entry.overlapped);
			return;
		}

		_entry = WatchEntry{};
	}

	// read/write 두 슬롯 모두 비활성이고 어느 쪽도 pending 이 아닐 때만 fd 전체(iocpSockets 포함)를 정리한다.
	void IocpEngine::ReleaseSocketContextIfIdle(const socket_t _fd) noexcept
	{
		const auto watch = watches.find(_fd);
		if (watch != watches.end() && watch->second.Empty() &&
			!watch->second.read.isPending && !watch->second.write.isPending)
		{
			ReleaseSocketContext(_fd);
		}
	}

	// zero-byte WSARecv/WSASend 를 (재)제출한다. 호출 시점에 이전 제출이 이미 완전히
	// 회수되어 있어야 한다(entry.pending == false) — 그렇지 않으면 overlapped 재사용으로
	// 커널과 경합한다.
	ne::Result<void, ne::OsError> IocpEngine::ArmWatch(WatchEntry& _entry) noexcept
	{
		_entry.overlapped = {};
		_entry.isPending = true;

		if (_entry.events & IoEvent::Read)
		{
			WSABUF wsaBuffer{ .len = 0, .buf = nullptr };
			ulong_t flags = 0;
			if (::WSARecv(_entry.fd, &wsaBuffer, 1, nullptr, &flags, &_entry.overlapped, nullptr) == SOCKET_ERROR)
			{
				if (const int error = ::WSAGetLastError(); error != WSA_IO_PENDING)
				{
					_entry.isPending = false;
					return ne::Result<void, ne::OsError>::Error(
						ne::OsError{ static_cast<ne::ulong_t>(error) }.Context("[IocpEngine/ArmWatch]"));
				}
			}
		}
		else if (_entry.events & IoEvent::Write)
		{
			WSABUF wsaBuffer{ .len = 0, .buf = nullptr };
			if (::WSASend(_entry.fd, &wsaBuffer, 1, nullptr, 0, &_entry.overlapped, nullptr) == SOCKET_ERROR)
			{
				if (const int error = ::WSAGetLastError(); error != WSA_IO_PENDING)
				{
					_entry.isPending = false;
					return ne::Result<void, ne::OsError>::Error(
						ne::OsError{ static_cast<ne::ulong_t>(error) }.Context("[IocpEngine/ArmWatch]"));
				}
			}
		}
		else
		{
			// Read/Write 어느 쪽도 요청하지 않음 — 제출할 I/O 없음.
			_entry.isPending = false;
		}

		return ne::Result<void, ne::OsError>::Ok();
	}



	ne::Result<void, ne::OsError> IocpEngine::SubmitSend(const socket_t _fd, const void* _buffer, const std::size_t _length, IoContext* _context) noexcept
	{
		std::lock_guard lock(mutex);

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
		std::lock_guard lock(mutex);

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
			const ne::int_t nextExpiry = timerWheel->NextExpiryMs();
			if (nextExpiry >= 0 && (effectiveTimeout == INFINITE || nextExpiry < effectiveTimeout))
			{
				effectiveTimeout = nextExpiry;
			}
		}

		ulong_t bytes = 0;
		ULONG_PTR key = 0;
		OVERLAPPED* overlapped = nullptr;

		// GQCS 자체는 락 밖에서 블로킹 — concurrentThreads > 1 이면 여러 스레드가 동시에 여기서
		// 대기할 수 있어야 한다(IOCP 의 표준 멀티스레드 워커 모델).
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
			// 주의: zero-byte 수신은 0바이트 전송으로 완료되므로 bytes==0 으로는 "데이터
			// 도착"과 "상대가 정상 종료(FIN)"를 구분할 수 없다 — Read 방향 성공 완료는 락
			// 밖에서 MSG_PEEK 으로 실제 확인한다(아래 needsPeekCheck 참고).
			auto* watchEntry = reinterpret_cast<WatchEntry*>(overlapped);

			// watches/iocpSockets 를 만지는 부분만 락으로 보호하고, 실제 콜백 호출(과 그 전의
			// MSG_PEEK 조회)은 반드시 락 밖에서 한다 — 콜백이 동기적으로 Watch()/Unwatch() 를
			// 재호출하는 경우가 실제로 있어(ReceiveAwaitable 등), 락을 쥔 채 부르면 같은
			// 스레드가 mutex 를 재진입해 데드락난다.
			IoCallback callback;
			uint32_t events = 0;
			socket_t fd = 0;
			bool_t invokeCallback = false;
			bool_t needsPeekCheck = false;

			{
				std::lock_guard lock(mutex);
				watchEntry->isPending = false;

				// Watch()/Unwatch() 가 이 overlapped 가 pending 인 동안 재등록/해제를 요청했던
				// 경우 — 지금 막 그 제출이 회수됐으므로 콜백을 부르지 않고 요청을 실행한다.
				// (완료가 실제 이벤트인지 CancelIoEx 로 인한 취소인지는 구분하지 않는다 —
				// 어느 쪽이든 호출자는 이미 최신 상태만 신경 쓴다.) 반대 방향 슬롯은 건드리지
				// 않으므로 fd 전체 정리는 두 슬롯이 모두 idle 일 때만 수행한다.
				if (watchEntry->pendingAction == WatchEntry::PendingAction::RELEASE)
				{
					fd = watchEntry->fd;
					*watchEntry = WatchEntry{};

					ReleaseSocketContextIfIdle(fd);
				}
				else if (watchEntry->pendingAction == WatchEntry::PendingAction::REARM)
				{
					watchEntry->pendingAction = WatchEntry::PendingAction::NONE;
					(void)ArmWatch(*watchEntry);
				}
				else if (!ok)
				{
					// 이 방향만 에러로 종료한다 — 반대 방향(다른 코루틴이 대기 중일 수 있음)은
					// 독립적으로 유지되며, 소켓이 실제로 죽었다면 그쪽도 곧 각자의 에러를 받는다.
					fd = watchEntry->fd;
					callback = std::move(watchEntry->callback);
					*watchEntry = WatchEntry{};
					events = IoEvent::Error | IoEvent::HangUp;
					invokeCallback = true;

					ReleaseSocketContextIfIdle(fd);
				}
				else
				{
					fd = watchEntry->fd;
					callback = watchEntry->callback; // 슬롯은 그대로 유지 — 호출자가 콜백 안에서 Unwatch 한다
					invokeCallback = true;

					if (watchEntry->events & IoEvent::Write)
						events = IoEvent::Write;
					else
						needsPeekCheck = true; // Read 방향 — 락 밖에서 MSG_PEEK 으로 판별
				}
			}

			if (invokeCallback)
			{
				if (needsPeekCheck)
				{
					// 데이터를 소비하지 않고 조회만 한다. 이 zero-byte 완료 자체가 "recv 가 즉시
					// 반환될 상태"라는 뜻이므로 MSG_PEEK 호출은 블로킹되지 않는다.
					char peekByte;
					const int peeked = ::recv(fd, &peekByte, 1, MSG_PEEK);
					if (peeked == 0)
						events = IoEvent::HangUp; // 상대가 정상 종료(FIN) — 더 읽을 데이터 없음
					else if (peeked == SOCKET_ERROR)
						events = IoEvent::Error | IoEvent::HangUp;
					else
						events = IoEvent::Read; // 진짜 데이터 도착
				}

				callback(fd, events);
			}
		}
		else // ProactorKey — 소켓 proactor 또는 파일 proactor. IoContext 는 코루틴이 소유하므로 watches/iocpSockets 를 만지지 않는다 — 락 불필요.
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
		// 이미 이 fd 로 등록된 적이 있으면 어떤 key 로 등록됐는지 대조한다 — 한 소켓에 Watch
		// (ReactorKey) 와 SubmitSend/SubmitReceive(ProactorKey) 를 섞어 쓰면 실제로는 최초
		// key 로만 연결된 채 남기 때문에, 완료가 엉뚱한 GQCS 분기로 들어가 OVERLAPPED* 를
		// 잘못된 타입으로 reinterpret_cast 하게 된다 — 조용히 무시하지 않고 에러로 거부한다.
		if (const auto it = iocpSockets.find(_fd); it != iocpSockets.end())
		{
			if (it->second != _key)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ ERROR_INVALID_PARAMETER, "socket already registered under a different IOCP key (Reactor/Proactor mix)" }
						.Context("[IocpEngine/EnsureSocketInIocp]"));

			return ne::Result<void, ne::OsError>::Ok();
		}

		const HANDLE associate = ::CreateIoCompletionPort(reinterpret_cast<HANDLE>(_fd), iocpHandle.Get(), _key, 0);
		if (!associate)
		{
			// ERROR_INVALID_PARAMETER: 이미 이 IOCP 에 연결된 소켓 — 정상으로 처리
			if (const ulong_t error = ::GetLastError(); error != ERROR_INVALID_PARAMETER)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ error }.Context("[IocpEngine/EnsureSocketInIocp]"));
		}

		iocpSockets[_fd] = _key;

		return ne::Result<void, ne::OsError>::Ok();
	}

	void IocpEngine::ReleaseSocketContext(const socket_t _fd)
	{
		// watches 뿐 아니라 iocpSockets 도 함께 지운다 — 그렇지 않으면 소켓이 닫힌 뒤
		// OS 가 같은 SOCKET 값을 재사용했을 때 EnsureSocketInIocp 가 "이미 등록됨"으로
		// 오판해 새 소켓을 IOCP 에 연결하지 않는다.
		// 아직 열려 있는 소켓에 대해 먼저 호출돼도 안전하다 — 다음 EnsureSocketInIocp
		// 호출이 CreateIoCompletionPort 를 재시도하고, ERROR_INVALID_PARAMETER(이미 연결됨)를
		// 정상 처리해 iocpSockets 에 다시 등록한다.
		watches.erase(_fd);
		iocpSockets.erase(_fd);
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

		if (!::ReadFile(_handle, _buffer, _length, nullptr, &_context->overlapped))
		{
			if (const ulong_t error = ::GetLastError(); error != ERROR_IO_PENDING)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ error }.Context("[IocpEngine/SubmitRead]"));
		}

		return ne::Result<void, ne::OsError>::Ok();
	}

	ne::Result<void, ne::OsError> IocpEngine::SubmitWrite(const HANDLE _handle, const void* _buffer, const std::size_t _length, const std::size_t _offset, IoContext* _context) noexcept
	{
		_context->overlapped = { .Offset = static_cast<ulong_t>(_offset & 0xFFFFFFFF), .OffsetHigh = static_cast<ulong_t>(_offset >> 32) };

		if (!::WriteFile(_handle, _buffer, _length, nullptr, &_context->overlapped))
		{
			if (const ulong_t error = ::GetLastError(); error != ERROR_IO_PENDING)
				return ne::Result<void, ne::OsError>::Error(
					ne::OsError{ error }.Context("[IocpEngine/SubmitWrite]"));
		}

		return ne::Result<void, ne::OsError>::Ok();
	}

END_NS

#endif // _WIN32
