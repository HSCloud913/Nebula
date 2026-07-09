//
// Created by hscloud on 26. 6. 30.
//

#include "Io/Engine/Iocp/IocpEngine.h"

#if defined(_WIN32)
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <mswsock.h>
#	include <cstdint>



// NTSTATUS(완료 entry.Internal) → Win32 에러코드 변환. ntdll 제공(선언만 직접 노출).
extern "C" ne::ulong_t __stdcall RtlNtStatusToDosError(ne::long_t _status);



BEGIN_NS(ne::io)
	IocpEngine::IocpEngine(const ulong_t _concurrentThreads) noexcept
		: iocpHandle(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, _concurrentThreads))
	{
		// RioProvider 는 lazy 초기화(WSAIoctl/RIOCreateCompletionQueue 는 RegisterBuffer 첫 호출 때만) —
		// 여기서 만들어 둬도 비용은 iocp 핸들/키 저장뿐이다.
		if (iocpHandle) rioProvider = std::make_unique<RioProvider>(iocpHandle.Get(), RioKey);
	}



	void_t IocpEngine::Submit(const Request& _request)
	{
		// RIO(SendZeroCopy) 는 자체 완료 큐(RIO_CQ→IOCP 바인딩, RioKey)를 쓴다 — IocpOperation/
		// OVERLAPPED 를 전혀 만들지 않고 곧장 RioProvider 로 제출한다. userData 는 RequestContext 로
		// 그대로 실려 WaitCompletions()→DrainRioCompletions() 가 Completion 으로 되돌려준다.
		// (Cancel() 은 이 경로를 추적하지 않는다 — RIO 제출 취소는 이번 범위 밖.)
		if (_request.op == OpCode::SendZeroCopy)
		{
			auto result = rioProvider->SubmitSendRegistered(static_cast<socket_t>(_request.handle),
															BufferHandle{ _request.bufferId }, _request.buffer, _request.length, _request.userData);
			if (result.IsError())
			{
				auto* failure = new IocpOperation{};
				failure->userData = _request.userData;
				failure->hasSyncResult = true;
				failure->syncResult = -static_cast<longlong_t>(result.Error().Code());
				::PostQueuedCompletionStatus(iocpHandle.Get(), 0, 0, &failure->overlapped);
			}
			return;
		}

		auto* operation = new IocpOperation{};
		operation->userData = _request.userData;
		operation->op = _request.op;
		// 파일 오프셋(소켓 op 에서는 무시된다).
		operation->overlapped.Offset = static_cast<ulong_t>(_request.offset & 0xFFFFFFFFull);
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

	int_t IocpEngine::WaitCompletions(Completion* _out, const int_t _max, const std::chrono::milliseconds _timeout)
	{
		if (_max <= 0) return 0;

		const int_t capacity = _max < MaxBatch ? _max : MaxBatch;
		OVERLAPPED_ENTRY entries[MaxBatch];
		ulong_t removed = 0;
		const ulong_t timeoutMs = _timeout.count() < 0 ? INFINITE : static_cast<ulong_t>(_timeout.count());

		if (!::GetQueuedCompletionStatusEx(iocpHandle.Get(), entries, static_cast<ulong_t>(capacity), &removed, timeoutMs, FALSE)) return 0; // 타임아웃 등 — 수확할 완료 없음

		int_t count = 0;
		for (ulong_t i = 0; i < removed && count < _max; ++i)
		{
			// RIO 완료 통지 — lpOverlapped 는 RioProvider 소유의 notifyOverlapped 를 가리키므로
			// IocpOperation* 으로 캐스팅하면 안 된다(먼저 key 로 구분해야 함).
			if (entries[i].lpCompletionKey == RioKey)
			{
				count += DrainRioCompletions(_out + count, _max - count);
				continue;
			}

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
			if (operation->hasSyncResult) result = operation->syncResult;
			else if (const long_t status = static_cast<long_t>(entries[i].Internal); status < 0) result = -static_cast<longlong_t>(::RtlNtStatusToDosError(status)); // 실패(NTSTATUS 음수) → Win32 에러
			else result = static_cast<longlong_t>(entries[i].dwNumberOfBytesTransferred);

			// Accept/Connect 완료 후처리 — SO_UPDATE_* 컨텍스트 갱신 및 Accept 는 새 소켓 핸들을 result 로.
			if (operation->op == OpCode::Accept)
			{
				if (result >= 0)
				{
					const SOCKET listenSocket = static_cast<SOCKET>(operation->contextSocket);
					(void_t)::setsockopt(static_cast<SOCKET>(operation->acceptSocket), SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
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
				(void_t)::setsockopt(static_cast<SOCKET>(operation->contextSocket), SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
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

	void_t IocpEngine::Cancel(void_t* _userData) noexcept
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
		case Capability::SendFileZeroCopy: return true;  // TransmitFile(SendFile)
		case Capability::SendMemZeroCopy: return true;  // RIO(SendZeroCopy) — RegisterBuffer 로 등록된 버퍼만
		case Capability::RecvOverheadReduced: return false; // ReadFixed/WriteFixed 는 일반 ReadFile/WriteFile 폴백(RIO 는 소켓 전용, 파일 등록버퍼 없음)
		case Capability::RecvTrueZeroCopy: return false; // Windows 에 진짜 recv zero-copy 없음
		}

		return false;
	}



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
		if (isExtensionsLoaded) return true;

		GUID acceptGuid = WSAID_ACCEPTEX;
		GUID connectGuid = WSAID_CONNECTEX;
		ulong_t bytes = 0;

		if (::WSAIoctl(_socket,
						SIO_GET_EXTENSION_FUNCTION_POINTER,
						&acceptGuid,
						sizeof(acceptGuid),
						&acceptExPtr,
						sizeof(acceptExPtr),
						&bytes,
						nullptr,
						nullptr) == SOCKET_ERROR)
			return false;

		if (::WSAIoctl(_socket,
						SIO_GET_EXTENSION_FUNCTION_POINTER,
						&connectGuid,
						sizeof(connectGuid),
						&connectExPtr,
						sizeof(connectExPtr),
						&bytes,
						nullptr,
						nullptr) == SOCKET_ERROR)
			return false;

		isExtensionsLoaded = true;

		return true;
	}

	void_t IocpEngine::Dispatch(IocpOperation* _operation, const Request& _request, const HANDLE _handle) noexcept
	{
		// 파일 scatter/gather(Readv/Writev) — Windows 는 ReadFileScatter/WriteFileGather 가 페이지 정렬을
		// 요구해 임의 크기 세그먼트에 못 쓴다. 세그먼트별로 순차 ReadFile/WriteFile+GetOverlappedResult(대기)
		// 를 이 스레드에서 동기적으로 수행해 합산한 뒤, 기존 "동기 완료를 IOCP 로 되돌리는" 경로(아래
		// syncError 처리와 동일한 메커니즘, 성공값도 실어 보낼 수 있음)로 결과를 넘긴다.
		if (_request.chain != nullptr && (_request.op == OpCode::Read || _request.op == OpCode::Write))
		{
			// hEvent 의 최하위 비트를 세우면(문서화된 트릭) 이 handle 이 IOCP 에 연결돼 있어도 이
			// OVERLAPPED 의 완료가 IOCP 로 자동 posting 되지 않는다 — 세팅 안 하면 세그먼트마다 쓰는
			// 스택 OVERLAPPED(다음 반복에서 소멸)를 가리키는 가짜 완료가 IOCP 에 쌓여 WaitCompletions
			// 가 나중에 댕글링 포인터를 완료로 잘못 처리한다. 단, hEvent 는 GetOverlappedResult(wait)
			// 의 실제 대기 객체이기도 하므로 값 자체가 유효한 이벤트 핸들이어야 한다(1 같은 가짜 값은
			// WaitForSingleObject 에서 ERROR_INVALID_HANDLE) — bit0 은 커널이 IOCP posting 여부를 볼 때만
			// 마스킹해서 보고, 대기 시에는 그대로 실핸들로 쓰인다.
			const HANDLE waitEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (waitEvent == nullptr)
			{
				_operation->hasSyncResult = true;
				_operation->syncResult = -static_cast<longlong_t>(::GetLastError());
				::PostQueuedCompletionStatus(iocpHandle.Get(), 0, 0, &_operation->overlapped);
				return;
			}

			longlong_t total = 0;
			ulonglong_t currentOffset = _request.offset;
			bool_t hasFailed = false;
			ulong_t lastError = 0;

			const auto taggedEvent = reinterpret_cast<HANDLE>(reinterpret_cast<std::uintptr_t>(waitEvent) | 1);
			for (const auto& segment : _request.chain->Segments())
			{
				OVERLAPPED segmentOverlapped{};
				segmentOverlapped.Offset = static_cast<ulong_t>(currentOffset & 0xFFFFFFFFull);
				segmentOverlapped.OffsetHigh = static_cast<ulong_t>(currentOffset >> 32);
				segmentOverlapped.hEvent = taggedEvent;

				ulong_t transferred = 0;
				bool_t isOk = (_request.op == OpCode::Read)
								? ::ReadFile(_handle, segment.ptr, static_cast<ulong_t>(segment.length), &transferred, &segmentOverlapped)
								: ::WriteFile(_handle, segment.ptr, static_cast<ulong_t>(segment.length), &transferred, &segmentOverlapped);
				if (!isOk && ::GetLastError() == ERROR_IO_PENDING)
				{
					isOk = ::GetOverlappedResult(_handle, &segmentOverlapped, &transferred, TRUE);
					::ResetEvent(waitEvent); // 수동 리셋 이벤트 — 다음 세그먼트를 위해 되돌림
				}

				if (!isOk)
				{
					hasFailed = true;
					lastError = ::GetLastError();
					break;
				}

				total += static_cast<longlong_t>(transferred);
				currentOffset += transferred;
				if (transferred < segment.length) break; // 짧은 전송 — 나머지는 호출자가 Suffix() 로 재시도
			}

			::CloseHandle(waitEvent);

			_operation->hasSyncResult = true;
			_operation->syncResult = hasFailed ? -static_cast<longlong_t>(lastError) : total;

			::PostQueuedCompletionStatus(iocpHandle.Get(), 0, 0, &_operation->overlapped);

			return;
		}

		int_t syncError = 0; // 0 = 즉시 실패 없음(완료가 IOCP 로 옴)

		switch (_request.op)
		{
		case OpCode::Read:
		{
			ulong_t read = 0;
			if (!::ReadFile(_handle, _request.buffer, static_cast<ulong_t>(_request.length), &read, &_operation->overlapped)) if (const ulong_t error = ::GetLastError(); error != ERROR_IO_PENDING) syncError = static_cast<int_t>(error);
			break;
		}
		case OpCode::Write:
		{
			ulong_t written = 0;
			if (!::WriteFile(_handle, _request.buffer, static_cast<ulong_t>(_request.length), &written, &_operation->overlapped)) if (const ulong_t error = ::GetLastError(); error != ERROR_IO_PENDING) syncError = static_cast<int_t>(error);
			break;
		}
		case OpCode::Receive:
		{
			ulong_t received = 0;
			ulong_t flags = 0;

			int_t rc;
			if (_request.chain != nullptr)
			{
				_operation->wsaBuffers.reserve(_request.chain->Segments().size());
				for (const auto& segment : _request.chain->Segments()) _operation->wsaBuffers.push_back(WSABUF{ .len = static_cast<ulong_t>(segment.length), .buf = reinterpret_cast<lpstr_t>(segment.ptr) });
				rc = ::WSARecv(reinterpret_cast<SOCKET>(_handle), _operation->wsaBuffers.data(), static_cast<ulong_t>(_operation->wsaBuffers.size()), &received, &flags, &_operation->overlapped, nullptr);
			}
			else
			{
				WSABUF wsaBuffer{ .len = static_cast<ulong_t>(_request.length), .buf = static_cast<lpstr_t>(_request.buffer) };
				rc = ::WSARecv(reinterpret_cast<SOCKET>(_handle), &wsaBuffer, 1, &received, &flags, &_operation->overlapped, nullptr);
			}

			if (rc == SOCKET_ERROR) if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;

			break;
		}
		case OpCode::Send:
		{
			ulong_t sent = 0;

			int_t rc;
			if (_request.chain != nullptr)
			{
				_operation->wsaBuffers.reserve(_request.chain->Segments().size());
				for (const auto& segment : _request.chain->Segments()) _operation->wsaBuffers.push_back(WSABUF{ .len = static_cast<ulong_t>(segment.length), .buf = reinterpret_cast<lpstr_t>(segment.ptr) });
				rc = ::WSASend(reinterpret_cast<SOCKET>(_handle), _operation->wsaBuffers.data(), static_cast<ulong_t>(_operation->wsaBuffers.size()), &sent, 0, &_operation->overlapped, nullptr);
			}
			else
			{
				WSABUF wsaBuffer{ .len = static_cast<ulong_t>(_request.length), .buf = static_cast<lpstr_t>(_request.buffer) };
				rc = ::WSASend(reinterpret_cast<SOCKET>(_handle), &wsaBuffer, 1, &sent, 0, &_operation->overlapped, nullptr);
			}

			if (rc == SOCKET_ERROR) if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;

			break;
		}
		case OpCode::SendTo: // 비연결형(UDP) 송신 — address/addressLength 가 매 호출 목적지
		{
			WSABUF wsaBuffer{ .len = static_cast<ulong_t>(_request.length), .buf = static_cast<lpstr_t>(_request.buffer) };
			ulong_t sent = 0;
			if (::WSASendTo(reinterpret_cast<SOCKET>(_handle), &wsaBuffer, 1, &sent, 0,
			                 static_cast<const sockaddr*>(_request.address), _request.addressLength,
			                 &_operation->overlapped, nullptr) == SOCKET_ERROR)
				if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;
			break;
		}
		case OpCode::ReceiveFrom: // 비연결형(UDP) 수신 — fromAddress/fromAddressLength 에 발신자 주소를 채움
		{
			WSABUF wsaBuffer{ .len = static_cast<ulong_t>(_request.length), .buf = static_cast<lpstr_t>(_request.buffer) };
			ulong_t received = 0;
			ulong_t flags = 0;
			if (::WSARecvFrom(reinterpret_cast<SOCKET>(_handle), &wsaBuffer, 1, &received, &flags,
			                   static_cast<sockaddr*>(_request.fromAddress), _request.fromAddressLength,
			                   &_operation->overlapped, nullptr) == SOCKET_ERROR)
				if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;
			break;
		}
		case OpCode::Accept:
		{
			const SOCKET listenSocket = reinterpret_cast<SOCKET>(_handle);
			if (!EnsureExtensions(static_cast<socket_t>(listenSocket)))
			{
				syncError = ::WSAGetLastError();
				break;
			}

			// accept 소켓은 listen 소켓과 같은 주소 체계로 만든다(TCP 전제).
			sockaddr_storage local{};
			int_t nameLength = static_cast<int_t>(sizeof(local));
			(void_t)::getsockname(listenSocket, reinterpret_cast<sockaddr*>(&local), &nameLength);

			// io::Socket::Create 와 동일하게 항상 WSA_FLAG_REGISTERED_IO 로 만든다 — RIO(SendZeroCopy)는
			// 이 플래그로 만든 소켓에서만 동작하고 생성 시점에만 지정 가능하므로 옵트인 없이 항상 켠다.
			const SOCKET accepted = ::WSASocketW(local.ss_family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_REGISTERED_IO);
			if (accepted == INVALID_SOCKET)
			{
				syncError = ::WSAGetLastError();
				break;
			}

			_operation->acceptSocket = static_cast<socket_t>(accepted);
			_operation->contextSocket = static_cast<socket_t>(listenSocket);

			const auto acceptEx = reinterpret_cast<LPFN_ACCEPTEX>(acceptExPtr);
			const ulong_t addressLength = static_cast<ulong_t>(sizeof(sockaddr_in6) + 16);

			ulong_t received = 0;
			if (!acceptEx(listenSocket, accepted, _operation->acceptBuffer, 0, addressLength, addressLength, &received, &_operation->overlapped)) if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;

			break;
		}
		case OpCode::Connect:
		{
			const SOCKET socket = reinterpret_cast<SOCKET>(_handle);
			if (!EnsureExtensions(static_cast<socket_t>(socket)))
			{
				syncError = ::WSAGetLastError();
				break;
			}

			const auto* target = static_cast<const sockaddr*>(_request.address);
			if (target == nullptr)
			{
				syncError = static_cast<int_t>(WSAEINVAL);
				break;
			}

			// ConnectEx 는 소켓이 bound 되어 있어야 한다 — 대상 family 로 any:0 바인딩(이미 bound 면 무시).
			sockaddr_storage local{};
			local.ss_family = target->sa_family;

			const int_t bindLength = (target->sa_family == AF_INET6) ? static_cast<int_t>(sizeof(sockaddr_in6)) : static_cast<int_t>(sizeof(sockaddr_in));
			(void_t)::bind(socket, reinterpret_cast<sockaddr*>(&local), bindLength);

			_operation->contextSocket = static_cast<socket_t>(socket);

			const auto connectEx = reinterpret_cast<LPFN_CONNECTEX>(connectExPtr);
			ulong_t sent = 0;
			if (!connectEx(socket, target, _request.addressLength, nullptr, 0, &sent, &_operation->overlapped)) if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;

			break;
		}
		case OpCode::SendFile: // handle=목적지 소켓(Send 계열과 동일), auxHandle=원본 파일. TransmitFile 은
		{                      // OVERLAPPED.Offset/OffsetHigh 로 시작 오프셋을 받는다(위에서 이미 세팅됨).
			const SOCKET destSocket = reinterpret_cast<SOCKET>(_handle);
			const auto sourceFile = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(_request.auxHandle));
			if (!::TransmitFile(destSocket, sourceFile, static_cast<ulong_t>(_request.length), 0, &_operation->overlapped, nullptr, 0)) if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;
			break;
		}
		case OpCode::ReadFixed: // Windows 에 파일 등록 버퍼 개념이 없다(RIO 는 소켓 전용) — 일반 Read 와 동일, bufferId 무시
		{
			ulong_t read = 0;
			if (!::ReadFile(_handle, _request.buffer, static_cast<ulong_t>(_request.length), &read, &_operation->overlapped)) if (const ulong_t error = ::GetLastError(); error != ERROR_IO_PENDING) syncError = static_cast<int_t>(error);
			break;
		}
		case OpCode::WriteFixed: // 위와 동일
		{
			ulong_t written = 0;
			if (!::WriteFile(_handle, _request.buffer, static_cast<ulong_t>(_request.length), &written, &_operation->overlapped)) if (const ulong_t error = ::GetLastError(); error != ERROR_IO_PENDING) syncError = static_cast<int_t>(error);
			break;
		}
		default: syncError = static_cast<int_t>(ERROR_NOT_SUPPORTED); // 알 수 없는 op
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

	int_t IocpEngine::DrainRioCompletions(Completion* _out, const int_t _max) noexcept
	{
		if (rioProvider == nullptr || !rioProvider->IsInitialized() || _max <= 0) return 0;

		RIORESULT results[MaxBatch];
		const ulong_t capacity = static_cast<ulong_t>(_max < MaxBatch ? _max : MaxBatch);
		const ulong_t dequeued = rioProvider->Table().RIODequeueCompletion(rioProvider->CompletionQueue(), results, capacity);

		int_t count = 0;
		if (dequeued != 0 && dequeued != RIO_CORRUPT_CQ)
		{
			for (ulong_t i = 0; i < dequeued; ++i)
			{
				const longlong_t result = results[i].Status == 0
											? static_cast<longlong_t>(results[i].BytesTransferred)
											: -static_cast<longlong_t>(::RtlNtStatusToDosError(static_cast<long_t>(results[i].Status)));

				_out[count].userData = reinterpret_cast<void_t*>(static_cast<std::uintptr_t>(results[i].RequestContext));
				_out[count].result = result;
				++count;
			}
		}

		(void_t)rioProvider->ArmNotify(); // RIONotify 는 one-shot — 다음 완료를 받으려면 매번 재무장

		return count;
	}

END_NS

#endif // _WIN32
