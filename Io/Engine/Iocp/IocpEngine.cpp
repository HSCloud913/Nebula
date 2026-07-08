//
// Created by hscloud on 26. 6. 30.
//

#include "IocpEngine.h"

#if defined(_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

// NTSTATUS(완료 entry.Internal) → Win32 에러코드 변환. ntdll 제공(선언만 직접 노출).
extern "C" ne::ulong_t __stdcall RtlNtStatusToDosError(ne::long_t _status);

BEGIN_NS(ne::io)
	IocpEngine::IocpEngine(const ulong_t _concurrentThreads) noexcept
		: iocpHandle(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, _concurrentThreads)) {}

	bool_t IocpEngine::EnsureAssociated(const HANDLE _handle) noexcept
	{
		const ulonglong_t key = reinterpret_cast<ulonglong_t>(_handle);
		if (associated.contains(key)) return true;

		// completionKey 는 0(op 완료). Wake 는 널 overlapped 로 구분하므로 키로 나눌 필요 없다.
		if (::CreateIoCompletionPort(_handle, iocpHandle.Get(), 0, 0) == nullptr) return false;

		associated.insert(key);
		return true;
	}

	bool_t IocpEngine::EnsureExtensions(const socket_t _socket) noexcept
	{
		std::lock_guard lock(mutex);
		if (extensionsLoaded) return true;

		GUID acceptGuid = WSAID_ACCEPTEX;
		GUID connectGuid = WSAID_CONNECTEX;
		ulong_t bytes = 0;

		if (::WSAIoctl(_socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &acceptGuid, sizeof(acceptGuid),
		               &acceptExPtr, sizeof(acceptExPtr), &bytes, nullptr, nullptr) == SOCKET_ERROR) return false;
		if (::WSAIoctl(_socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &connectGuid, sizeof(connectGuid),
		               &connectExPtr, sizeof(connectExPtr), &bytes, nullptr, nullptr) == SOCKET_ERROR) return false;

		extensionsLoaded = true;
		return true;
	}

	void_t IocpEngine::Dispatch(IocpOperation* _operation, const IoRequest& _request, const HANDLE _handle) noexcept
	{
		int_t syncError = 0; // 0 = 즉시 실패 없음(완료가 IOCP 로 옴)

		switch (_request.op)
		{
		case OpCode::Read:
		{
			ulong_t read = 0;
			if (!::ReadFile(_handle, _request.buffer, static_cast<ulong_t>(_request.length), &read, &_operation->overlapped))
				if (const ulong_t error = ::GetLastError(); error != ERROR_IO_PENDING) syncError = static_cast<int_t>(error);
			break;
		}
		case OpCode::Write:
		{
			ulong_t written = 0;
			if (!::WriteFile(_handle, _request.buffer, static_cast<ulong_t>(_request.length), &written, &_operation->overlapped))
				if (const ulong_t error = ::GetLastError(); error != ERROR_IO_PENDING) syncError = static_cast<int_t>(error);
			break;
		}
		case OpCode::Receive:
		{
			WSABUF wsaBuffer{ .len = static_cast<ulong_t>(_request.length), .buf = static_cast<lpstr_t>(_request.buffer) };
			ulong_t received = 0;
			ulong_t flags = 0;
			if (::WSARecv(reinterpret_cast<SOCKET>(_handle), &wsaBuffer, 1, &received, &flags, &_operation->overlapped, nullptr) == SOCKET_ERROR)
				if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;
			break;
		}
		case OpCode::Send:
		{
			WSABUF wsaBuffer{ .len = static_cast<ulong_t>(_request.length), .buf = static_cast<lpstr_t>(_request.buffer) };
			ulong_t sent = 0;
			if (::WSASend(reinterpret_cast<SOCKET>(_handle), &wsaBuffer, 1, &sent, 0, &_operation->overlapped, nullptr) == SOCKET_ERROR)
				if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;
			break;
		}
		case OpCode::Accept:
		{
			const SOCKET listenSocket = reinterpret_cast<SOCKET>(_handle);
			if (!EnsureExtensions(static_cast<socket_t>(listenSocket))) { syncError = ::WSAGetLastError(); break; }

			// accept 소켓은 listen 소켓과 같은 주소 체계로 만든다(TCP 전제).
			sockaddr_storage local{};
			int_t nameLength = static_cast<int_t>(sizeof(local));
			(void)::getsockname(listenSocket, reinterpret_cast<sockaddr*>(&local), &nameLength);

			const SOCKET accepted = ::WSASocketW(local.ss_family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
			if (accepted == INVALID_SOCKET) { syncError = ::WSAGetLastError(); break; }

			_operation->acceptSocket = static_cast<socket_t>(accepted);
			_operation->contextSocket = static_cast<socket_t>(listenSocket);

			const auto acceptEx = reinterpret_cast<LPFN_ACCEPTEX>(acceptExPtr);
			const ulong_t addressLength = static_cast<ulong_t>(sizeof(sockaddr_in6) + 16);
			ulong_t received = 0;
			if (!acceptEx(listenSocket, accepted, _operation->acceptBuffer, 0, addressLength, addressLength, &received, &_operation->overlapped))
				if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;
			break;
		}
		case OpCode::Connect:
		{
			const SOCKET socket = reinterpret_cast<SOCKET>(_handle);
			if (!EnsureExtensions(static_cast<socket_t>(socket))) { syncError = ::WSAGetLastError(); break; }

			const auto* target = static_cast<const sockaddr*>(_request.address);
			if (target == nullptr) { syncError = static_cast<int_t>(WSAEINVAL); break; }

			// ConnectEx 는 소켓이 bound 되어 있어야 한다 — 대상 family 로 any:0 바인딩(이미 bound 면 무시).
			sockaddr_storage local{};
			local.ss_family = target->sa_family;
			const int_t bindLength = (target->sa_family == AF_INET6) ? static_cast<int_t>(sizeof(sockaddr_in6)) : static_cast<int_t>(sizeof(sockaddr_in));
			(void)::bind(socket, reinterpret_cast<sockaddr*>(&local), bindLength);

			_operation->contextSocket = static_cast<socket_t>(socket);

			const auto connectEx = reinterpret_cast<LPFN_CONNECTEX>(connectExPtr);
			ulong_t sent = 0;
			if (!connectEx(socket, target, _request.addressLength, nullptr, 0, &sent, &_operation->overlapped))
				if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;
			break;
		}
		default:
			// *_fixed / ZeroCopy / SendFile(Level 3.5)는 후속 Phase 에서 구현.
			syncError = static_cast<int_t>(ERROR_NOT_SUPPORTED);
			break;
		}

		if (syncError != 0)
		{
			// 동기 실패 — 완료가 IOCP 로 오지 않으므로 -에러를 담아 수동으로 되돌린다(경로 통일).
			_operation->hasSyncResult = true;
			_operation->syncResult = -static_cast<longlong_t>(syncError);
			::PostQueuedCompletionStatus(iocpHandle.Get(), 0, 0, &_operation->overlapped);
		}
		// else: 성공/IO_PENDING — 완료는 WaitCompletions 에서 회수하며 operation 을 해제한다.
	}

	void_t IocpEngine::Submit(const IoRequest& _request)
	{
		auto* operation = new IocpOperation{};
		operation->userData = _request.userData;
		operation->op = _request.op;
		// 파일 오프셋(소켓 op 에서는 무시된다).
		operation->overlapped.Offset     = static_cast<ulong_t>(_request.offset & 0xFFFFFFFFull);
		operation->overlapped.OffsetHigh = static_cast<ulong_t>(_request.offset >> 32);

		const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(_request.handle));
		operation->handle = handle;

		{
			std::lock_guard lock(mutex);
			if (!EnsureAssociated(handle))
			{
				const int_t error = static_cast<int_t>(::GetLastError());
				operation->hasSyncResult = true;
				operation->syncResult = -static_cast<longlong_t>(error);
				::PostQueuedCompletionStatus(iocpHandle.Get(), 0, 0, &operation->overlapped);
				return;
			}

			// Cancel 이 조회할 수 있도록 커널 제출 전에 등록한다(제출 후 완료가 먼저 오는 경합 방지).
			if (_request.userData != nullptr) inflight[_request.userData] = operation;
		}

		Dispatch(operation, _request, handle);
	}

	int_t IocpEngine::WaitCompletions(IoCompletion* _out, const int_t _max, const std::chrono::milliseconds _timeout)
	{
		if (_max <= 0) return 0;

		const int_t capacity = _max < MaxBatch ? _max : MaxBatch;
		OVERLAPPED_ENTRY entries[MaxBatch];
		ulong_t removed = 0;
		const ulong_t timeoutMs = _timeout.count() < 0 ? INFINITE : static_cast<ulong_t>(_timeout.count());

		if (!::GetQueuedCompletionStatusEx(iocpHandle.Get(), entries, static_cast<ulong_t>(capacity), &removed, timeoutMs, FALSE))
			return 0; // 타임아웃 등 — 수확할 완료 없음

		int_t count = 0;
		for (ulong_t i = 0; i < removed; ++i)
		{
			OVERLAPPED* overlapped = entries[i].lpOverlapped;
			if (overlapped == nullptr) continue; // Wake — 완료 아님

			auto* operation = reinterpret_cast<IocpOperation*>(overlapped);

			// Cancel 조회 대상에서 먼저 제거 — 이후 delete 해도 Cancel 이 dangling 을 만지지 않는다.
			if (operation->userData != nullptr)
			{
				std::lock_guard lock(mutex);
				inflight.erase(operation->userData);
			}

			longlong_t result;
			if (operation->hasSyncResult)
				result = operation->syncResult;
			else if (const long_t status = static_cast<long_t>(entries[i].Internal); status < 0)
				result = -static_cast<longlong_t>(::RtlNtStatusToDosError(status)); // 실패(NTSTATUS 음수) → Win32 에러
			else
				result = static_cast<longlong_t>(entries[i].dwNumberOfBytesTransferred);

			// Accept/Connect 완료 후처리 — SO_UPDATE_* 컨텍스트 갱신 및 Accept 는 새 소켓 핸들을 result 로.
			if (operation->op == OpCode::Accept)
			{
				if (result >= 0)
				{
					const SOCKET listenSocket = static_cast<SOCKET>(operation->contextSocket);
					(void)::setsockopt(static_cast<SOCKET>(operation->acceptSocket), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
					                   reinterpret_cast<const char*>(&listenSocket), static_cast<int_t>(sizeof(listenSocket)));
					result = static_cast<longlong_t>(operation->acceptSocket); // 성공: accept 소켓 핸들 반환
				}
				else
				{
					::closesocket(static_cast<SOCKET>(operation->acceptSocket)); // 실패: 만들어둔 소켓 정리
				}
			}
			else if (operation->op == OpCode::Connect && result >= 0)
			{
				(void)::setsockopt(static_cast<SOCKET>(operation->contextSocket), SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
				result = 0;
			}

			_out[count].userData = operation->userData;
			_out[count].result = result;
			++count;

			delete operation;
		}

		return count;
	}

	void_t IocpEngine::Wake()
	{
		::PostQueuedCompletionStatus(iocpHandle.Get(), 0, WakeKey, nullptr);
	}

	void_t IocpEngine::Cancel(void* _userData) noexcept
	{
		if (_userData == nullptr) return;

		// 락을 쥔 채 CancelIoEx 까지 수행한다 — WaitCompletions 의 erase+delete 와 직렬화되어
		// 취소 대상 op 가 그 사이 해제되지 않는다(이미 완료돼 erase 됐으면 조회 실패로 no-op).
		std::lock_guard lock(mutex);
		const auto iterator = inflight.find(_userData);
		if (iterator == inflight.end()) return;

		IocpOperation* operation = iterator->second;
		::CancelIoEx(operation->handle, &operation->overlapped);
	}

	bool_t IocpEngine::Supports(const Capability _capability) const noexcept
	{
		switch (_capability)
		{
		case Capability::SendFileZeroCopy:    return true;  // TransmitFile
		case Capability::SendMemZeroCopy:     return true;  // RIO
		case Capability::RecvOverheadReduced: return true;  // RIO
		case Capability::RecvTrueZeroCopy:    return false; // Windows 에 진짜 recv zero-copy 없음
		}

		return false;
	}

END_NS

#endif // _WIN32
