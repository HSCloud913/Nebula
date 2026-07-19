//
// Created by hscloud on 26. 6. 30.
//

#include "Io/Engine/Iocp/IocpEngine.h"

#if defined(_WIN32)
#	include <winsock2.h>
#	include <ws2tcpip.h>



// ntdll 함수. OVERLAPPED_ENTRY::Internal 에 담기는 값은 NTSTATUS 이고, 사용자 코드가 흔히 다루는
// Win32 에러 코드(GetLastError 계열)와는 체계가 달라 이 함수로 변환해야 의미가 맞는다.
extern "C" ne::ulong_t __stdcall RtlNtStatusToDosError(ne::long_t _status);

BEGIN_NS(ne::io)
	IocpEngine::IocpEngine(const ulong_t _concurrentThreads) noexcept
		// hFile=INVALID_HANDLE_VALUE, ExistingCompletionPort=nullptr 조합은 "새 IOCP 를 만들기만 한다"는 뜻이다.
		: iocpHandle(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, _concurrentThreads))
	{
		// RIO 완료는 이 IOCP 핸들에 RioKey 로 바인딩되므로, RioProvider 생성 시점에 핸들을 넘겨준다.
		if (iocpHandle) rioProvider = std::make_unique<RioProvider>(iocpHandle.Get(), RioKey);
	}



	void_t IocpEngine::Submit(const Request& _request)
	{
		if (_request.requestKind == RequestKind::SEND_ZERO_COPY)
		{
			// RIO 경로는 IocpOperation/OVERLAPPED 를 쓰지 않고 RIO 자체 요청 큐로 직접 제출한다.
			auto result = rioProvider->SubmitSendRegistered(static_cast<socket_t>(_request.handle), BufferHandle{ _request.bufferId }, _request.buffer, _request.length, _request.userData);
			if (result.IsError())
			{
				// 제출 자체가 실패한 경우, RIO 완료 경로(DrainRioCompletions) 대신 여기서 임시
				// IocpOperation 을 만들어 일반 완료 큐로 실패를 게시해 WaitCompletions() 가 통일된
				// 방식으로 소비하게 한다.
				auto* failure = new IocpOperation{};
				failure->userData = _request.userData;
				failure->hasSyncResult = true;
				failure->syncResult = -static_cast<longlong_t>(result.Error().Code());
				::PostQueuedCompletionStatus(iocpHandle.Get(), 0, 0, &failure->overlapped);
			}
			return;
		}

		// IocpOperation 은 완료 시점까지 살아 있어야 하므로 heap 에 둔다. 첫 멤버가 OVERLAPPED 이므로
		// 커널이 돌려주는 OVERLAPPED* 를 그대로 IocpOperation* 로 reinterpret_cast 할 수 있다.
		auto* operation = new IocpOperation{};
		operation->userData = _request.userData;
		operation->requestKind = _request.requestKind;
		operation->overlapped.Offset = static_cast<ulong_t>(_request.offset & 0xFFFFFFFFull);
		operation->overlapped.OffsetHigh = static_cast<ulong_t>(_request.offset >> 32);

		const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(_request.handle));
		operation->handle = handle;

		{
			std::lock_guard lock(mutex);
			if (!EnsureAssociated(handle))
			{
				// 연관 자체가 실패하면 어떤 비동기 API 도 호출할 수 없으므로, 곧바로 동기 실패
				// 완료를 IOCP 에 게시해 정상 완료와 동일한 경로로 처리되게 한다.
				const int_t error = static_cast<int_t>(::GetLastError());
				operation->hasSyncResult = true;
				operation->syncResult = -static_cast<longlong_t>(error);
				::PostQueuedCompletionStatus(iocpHandle.Get(), 0, 0, &operation->overlapped);
				return;
			}

			// Cancel() 이 나중에 이 요청을 찾을 수 있도록 먼저 inflight 에 등록한 뒤 실제 호출을 수행한다.
			if (_request.userData != nullptr) inflight[_request.userData] = operation;
		}

		Dispatch(operation, _request, handle);
	}

	int_t IocpEngine::WaitCompletions(Completion* _out, const int_t _max, const std::chrono::milliseconds _timeout)
	{
		if (_max <= 0) return 0;

		// Ex 버전은 한 번의 대기로 최대 capacity 개의 완료를 배치 회수한다(완료마다 별도 대기하는
		// 구버전 GetQueuedCompletionStatus 보다 훨씬 효율적).
		const int_t capacity = _max < MaxBatch ? _max : MaxBatch;
		OVERLAPPED_ENTRY entries[MaxBatch];
		ulong_t removed = 0;
		const ulong_t timeoutMs = _timeout.count() < 0 ? INFINITE : static_cast<ulong_t>(_timeout.count());

		if (!::GetQueuedCompletionStatusEx(iocpHandle.Get(), entries, static_cast<ulong_t>(capacity), &removed, timeoutMs, FALSE)) return 0;

		int_t count = 0;
		for (ulong_t i = 0; i < removed && count < _max; ++i)
		{
			// RioKey 는 실제 op 완료가 아니라 "RIO_CQ 에 새 완료가 도착했다"는 신호일 뿐이므로
			// RIO 전용 큐에서 별도로 회수해야 한다.
			if (entries[i].lpCompletionKey == RioKey)
			{
				count += DrainRioCompletions(_out + count, _max - count);
				continue;
			}

			OVERLAPPED* overlapped = entries[i].lpOverlapped;
			if (overlapped == nullptr) continue;

			// IocpOperation 의 첫 멤버가 OVERLAPPED 이므로 포인터 값을 그대로 재해석해 원래 컨텍스트를 복원한다.
			auto* operation = reinterpret_cast<IocpOperation*>(overlapped);

			if (operation->userData != nullptr)
			{
				std::lock_guard lock(mutex);
				inflight.erase(operation->userData);
			}

			longlong_t result;
			if (operation->hasSyncResult) result = operation->syncResult;
			// OVERLAPPED_ENTRY::Internal 은 NTSTATUS 를 담고 있어 Win32 에러 코드로 변환해야 사용자에게 일관되게 노출할 수 있다.
			else if (const long_t status = static_cast<long_t>(entries[i].Internal); status < 0) result = -static_cast<longlong_t>(::RtlNtStatusToDosError(status));
			else result = static_cast<longlong_t>(entries[i].dwNumberOfBytesTransferred);

			if (operation->requestKind == RequestKind::ACCEPT)
			{
				if (result >= 0)
				{
					// AcceptEx 로 만든 소켓은 리스닝 소켓의 옵션(SO_UPDATE_ACCEPT_CONTEXT)을 상속받아야
					// getsockname/getpeername, setsockopt 상속 등이 리스닝 소켓과 동일하게 동작한다.
					const SOCKET listenSocket = static_cast<SOCKET>(operation->contextSocket);
					(void_t)::setsockopt(static_cast<SOCKET>(operation->acceptSocket),
										SOL_SOCKET,
										SO_UPDATE_ACCEPT_CONTEXT,
										reinterpret_cast<const char*>(&listenSocket),
										static_cast<int_t>(sizeof(listenSocket)));
					result = static_cast<longlong_t>(operation->acceptSocket);
				}
				else
				{
					// accept 실패 시 AcceptEx 호출 전에 미리 만들어둔 소켓을 반드시 직접 닫아야 한다(누수 방지).
					::closesocket(static_cast<SOCKET>(operation->acceptSocket));
				}
			}
			else if (operation->requestKind == RequestKind::CONNECT && result >= 0)
			{
				// ConnectEx 로 연결된 소켓도 SO_UPDATE_CONNECT_CONTEXT 를 설정해야 getpeername/send 등
				// 일반 connect() 소켓과 동일하게 동작한다.
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

	void_t IocpEngine::Cancel(void_t* _userData) noexcept
	{
		if (_userData == nullptr) return;

		std::lock_guard lock(mutex);
		const auto iterator = inflight.find(_userData);
		if (iterator == inflight.end()) return;

		// CancelIoEx 는 특정 OVERLAPPED 하나만 취소 대상으로 지정할 수 있어(2번째 인자가 nullptr 이면
		// 그 핸들의 모든 미완료 요청을 취소), 다른 요청에는 영향을 주지 않는다. 비동기 호출이므로
		// 여기서 반환되어도 즉시 취소가 끝났다는 보장은 없다.
		IocpOperation* operation = iterator->second;
		::CancelIoEx(operation->handle, &operation->overlapped);
	}

	bool_t IocpEngine::Supports(const Capability _capability) const noexcept
	{
		switch (_capability)
		{
			case Capability::SEND_FILE_ZERO_COPY:
				return true;
			case Capability::SEND_MEM_ZERO_COPY:
				return true;
			case Capability::RECEIVE_OVERHEAD_REDUCED:
				return false;
			case Capability::RECEIVE_TRUE_ZERO_COPY:
				return false;
		}

		return false;
	}



	bool_t IocpEngine::EnsureAssociated(const HANDLE _handle) noexcept
	{
		// 같은 핸들을 CreateIoCompletionPort 로 두 번 연관시키면 실패하므로, 이미 연관된 핸들을
		// associated 셋으로 추적해 최초 1회만 시도한다.
		const ulonglong_t key = reinterpret_cast<ulonglong_t>(_handle);
		if (associated.contains(key)) return true;

		if (::CreateIoCompletionPort(_handle, iocpHandle.Get(), 0, 0) == nullptr) return false;

		associated.insert(key);

		return true;
	}

	bool_t IocpEngine::EnsureExtensions(const socket_t _socket) noexcept
	{
		std::lock_guard lock(mutex);
		if (isExtensionsLoaded) return true;

		// AcceptEx/ConnectEx 는 Winsock 표준 API 가 아니라 WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER)
		// 로만 얻을 수 있는 벤더 확장 함수라서, 최초 사용 시점에 함수 포인터를 로드해 캐시한다.
		GUID acceptGuid = WSAID_ACCEPTEX;
		GUID connectGuid = WSAID_CONNECTEX;
		ulong_t bytes = 0;

		if (::WSAIoctl(_socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &acceptGuid, sizeof(acceptGuid), &acceptExPtr, sizeof(acceptExPtr), &bytes, nullptr, nullptr) == SOCKET_ERROR) return false;
		if (::WSAIoctl(_socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &connectGuid, sizeof(connectGuid), &connectExPtr, sizeof(connectExPtr), &bytes, nullptr, nullptr) == SOCKET_ERROR) return false;

		isExtensionsLoaded = true;

		return true;
	}

	void_t IocpEngine::Dispatch(IocpOperation* _operation, const Request& _request, const HANDLE _handle) noexcept
	{
		// ReadFile/WriteFile 은 여러 버퍼(체인)를 한 번의 호출로 받는 API 가 없으므로, 세그먼트별로
		// 순차 호출하고 그때그때 완료를 기다린 뒤(GetOverlappedResult) 합산 결과를 만든다. 이 경로만
		// 예외적으로 이 함수 안에서 "동기적으로" 끝내고 결과를 PostQueuedCompletionStatus 로 게시한다.
		if (_request.chain != nullptr && (_request.requestKind == RequestKind::READ || _request.requestKind == RequestKind::WRITE))
		{
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

			// hEvent 의 최하위 비트를 1로 세팅하면(문서화된 Windows 관례) 이 OVERLAPPED 가 완료되어도
			// 이미 IOCP 에 연관된 핸들이더라도 완료 패킷을 포트에 큐잉하지 않는다. 여기서는
			// GetOverlappedResult 로 직접 기다리므로 중복 완료 통지를 막기 위해 필요하다.
			const auto taggedEvent = reinterpret_cast<HANDLE>(reinterpret_cast<std::uintptr_t>(waitEvent) | 1);
			for (const auto& segment : _request.chain->Segments())
			{
				OVERLAPPED segmentOverlapped{};
				segmentOverlapped.Offset = static_cast<ulong_t>(currentOffset & 0xFFFFFFFFull);
				segmentOverlapped.OffsetHigh = static_cast<ulong_t>(currentOffset >> 32);
				segmentOverlapped.hEvent = taggedEvent;

				ulong_t transferred = 0;
				bool_t isOk = (_request.requestKind == RequestKind::READ) ?
								::ReadFile(_handle, segment.ptr, static_cast<ulong_t>(segment.length), &transferred, &segmentOverlapped) :
								::WriteFile(_handle, segment.ptr, static_cast<ulong_t>(segment.length), &transferred, &segmentOverlapped);
				if (!isOk && ::GetLastError() == ERROR_IO_PENDING)
				{
					// bWait=TRUE 로 완료까지 블로킹 대기한다(체인 처리는 세그먼트를 순서대로 끝내야 하므로).
					isOk = ::GetOverlappedResult(_handle, &segmentOverlapped, &transferred, TRUE);
					::ResetEvent(waitEvent);
				}

				if (!isOk)
				{
					hasFailed = true;
					lastError = ::GetLastError();
					break;
				}

				total += static_cast<longlong_t>(transferred);
				currentOffset += transferred;
				// 짧은 전송(파일 끝 도달 등)이면 나머지 세그먼트를 시도하지 않고 지금까지의 합계로 완료한다.
				if (transferred < segment.length) break;
			}

			::CloseHandle(waitEvent);

			_operation->hasSyncResult = true;
			_operation->syncResult = hasFailed ? -static_cast<longlong_t>(lastError) : total;

			::PostQueuedCompletionStatus(iocpHandle.Get(), 0, 0, &_operation->overlapped);

			return;
		}

		int_t syncError = 0;

		switch (_request.requestKind)
		{
			case RequestKind::ACCEPT:
			{
				const SOCKET listenSocket = reinterpret_cast<SOCKET>(_handle);
				if (!EnsureExtensions(static_cast<socket_t>(listenSocket)))
				{
					syncError = ::WSAGetLastError();
					break;
				}

				// AcceptEx 는 리스닝 소켓과 같은 주소체계(IPv4/IPv6)의 새 소켓을 미리 만들어 넘겨야 하므로,
				// 리스닝 소켓의 실제 바인딩 주소체계를 getsockname 으로 먼저 확인한다.
				sockaddr_storage local{};
				int_t nameLength = static_cast<int_t>(sizeof(local));
				(void_t)::getsockname(listenSocket, reinterpret_cast<sockaddr*>(&local), &nameLength);

				// RIO 로 이어질 accept 라면 WSA_FLAG_REGISTERED_IO 로 소켓을 만들어야 RIO 요청 큐를 붙일 수 있다.
				const SOCKET accepted = ::WSASocketW(local.ss_family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED | (_request.isRegisteredIo ? WSA_FLAG_REGISTERED_IO : 0));
				if (accepted == INVALID_SOCKET)
				{
					syncError = ::WSAGetLastError();
					break;
				}

				_operation->acceptSocket = static_cast<socket_t>(accepted);
				_operation->contextSocket = static_cast<socket_t>(listenSocket);

				const auto acceptEx = reinterpret_cast<LPFN_ACCEPTEX>(acceptExPtr);
				// AcceptEx 의 로컬/원격 주소 버퍼는 sockaddr 구조체 크기 + 16바이트 여유를 요구한다(문서화된 요구사항).
				const ulong_t addressLength = static_cast<ulong_t>(sizeof(sockaddr_in6) + 16);

				ulong_t received = 0;
				if (!acceptEx(listenSocket, accepted, _operation->acceptBuffer, 0, addressLength, addressLength, &received, &_operation->overlapped))
					if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;

				break;
			}
			case RequestKind::CONNECT:
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

				// ConnectEx 는 사전에 bind() 된 소켓만 받아들이므로(문서화된 제약), 대상 주소체계에 맞춰
				// 와일드카드 주소로 명시적으로 bind 해준다.
				sockaddr_storage local{};
				local.ss_family = target->sa_family;

				const int_t bindLength = (target->sa_family == AF_INET6) ? static_cast<int_t>(sizeof(sockaddr_in6)) : static_cast<int_t>(sizeof(sockaddr_in));
				(void_t)::bind(socket, reinterpret_cast<sockaddr*>(&local), bindLength);

				_operation->contextSocket = static_cast<socket_t>(socket);

				const auto connectEx = reinterpret_cast<LPFN_CONNECTEX>(connectExPtr);
				ulong_t sent = 0;
				if (!connectEx(socket, target, _request.addressLength, nullptr, 0, &sent, &_operation->overlapped))
					if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;

				break;
			}
			case RequestKind::READ:
			{
				ulong_t read = 0;
				if (!::ReadFile(_handle, _request.buffer, static_cast<ulong_t>(_request.length), &read, &_operation->overlapped))
					if (const ulong_t error = ::GetLastError(); error != ERROR_IO_PENDING) syncError = static_cast<int_t>(error);
				break;
			}
			case RequestKind::WRITE:
			{
				ulong_t written = 0;
				if (!::WriteFile(_handle, _request.buffer, static_cast<ulong_t>(_request.length), &written, &_operation->overlapped))
					if (const ulong_t error = ::GetLastError(); error != ERROR_IO_PENDING) syncError = static_cast<int_t>(error);
				break;
			}
			case RequestKind::READ_FIXED:
			{
				ulong_t read = 0;
				if (!::ReadFile(_handle, _request.buffer, static_cast<ulong_t>(_request.length), &read, &_operation->overlapped))
					if (const ulong_t error = ::GetLastError(); error != ERROR_IO_PENDING) syncError = static_cast<int_t>(error);
				break;
			}
			case RequestKind::WRITE_FIXED:
			{
				ulong_t written = 0;
				if (!::WriteFile(_handle, _request.buffer, static_cast<ulong_t>(_request.length), &written, &_operation->overlapped))
					if (const ulong_t error = ::GetLastError(); error != ERROR_IO_PENDING) syncError = static_cast<int_t>(error);
				break;
			}
			case RequestKind::WAIT_READABLE:
			{
				ulong_t received = 0;
				ulong_t flags = 0;
				WSABUF wsaBuffer{ .len = 0, .buf = nullptr };
				if (::WSARecv(reinterpret_cast<SOCKET>(_handle), &wsaBuffer, 1, &received, &flags, &_operation->overlapped, nullptr) == SOCKET_ERROR)
					if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;
				break;
			}
			case RequestKind::WAIT_WRITABLE:
			{
				_operation->hasSyncResult = true;
				_operation->syncResult = 0;
				::PostQueuedCompletionStatus(iocpHandle.Get(), 0, 0, &_operation->overlapped);
				break;
			}
			case RequestKind::RECEIVE:
			{
				ulong_t received = 0;
				ulong_t flags = 0;

				int_t rc;
				if (_request.chain != nullptr)
				{
					_operation->wsaBuffers.reserve(_request.chain->Segments().size());
					for (const auto& segment : _request.chain->Segments()) _operation->wsaBuffers.push_back(WSABUF{ .len = static_cast<ulong_t>(segment.length), .buf = reinterpret_cast<lpstr_t>(segment.ptr) });
					rc = ::WSARecv(reinterpret_cast<SOCKET>(_handle),
									_operation->wsaBuffers.data(),
									static_cast<ulong_t>(_operation->wsaBuffers.size()),
									&received,
									&flags,
									&_operation->overlapped,
									nullptr);
				}
				else
				{
					WSABUF wsaBuffer{ .len = static_cast<ulong_t>(_request.length), .buf = static_cast<lpstr_t>(_request.buffer) };
					rc = ::WSARecv(reinterpret_cast<SOCKET>(_handle), &wsaBuffer, 1, &received, &flags, &_operation->overlapped, nullptr);
				}

				if (rc == SOCKET_ERROR) if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;

				break;
			}
			case RequestKind::SEND:
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
			case RequestKind::RECEIVE_FROM:
			{
				WSABUF wsaBuffer{ .len = static_cast<ulong_t>(_request.length), .buf = static_cast<lpstr_t>(_request.buffer) };
				ulong_t received = 0;
				ulong_t flags = 0;
				if (::WSARecvFrom(reinterpret_cast<SOCKET>(_handle),
								&wsaBuffer,
								1,
								&received,
								&flags,
								static_cast<sockaddr*>(_request.fromAddress),
								_request.fromAddressLength,
								&_operation->overlapped,
								nullptr) == SOCKET_ERROR)
					if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;
				break;
			}
			case RequestKind::SEND_TO:
			{
				WSABUF wsaBuffer{ .len = static_cast<ulong_t>(_request.length), .buf = static_cast<lpstr_t>(_request.buffer) };
				ulong_t sent = 0;
				if (::WSASendTo(reinterpret_cast<SOCKET>(_handle), &wsaBuffer, 1, &sent, 0, static_cast<const sockaddr*>(_request.address), _request.addressLength, &_operation->overlapped, nullptr) ==
					SOCKET_ERROR)
					if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;
				break;
			}
			case RequestKind::SEND_FILE:
			{
				const SOCKET destSocket = reinterpret_cast<SOCKET>(_handle);
				const auto sourceFile = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(_request.auxHandle));
				if (!::TransmitFile(destSocket, sourceFile, static_cast<ulong_t>(_request.length), 0, &_operation->overlapped, nullptr, 0))
					if (const int_t error = ::WSAGetLastError(); error != WSA_IO_PENDING) syncError = error;
				break;
			}
			default:
				syncError = static_cast<int_t>(ERROR_NOT_SUPPORTED);
				break;
		}

		if (syncError != 0)
		{
			_operation->hasSyncResult = true;
			_operation->syncResult = -static_cast<longlong_t>(syncError);
			::PostQueuedCompletionStatus(iocpHandle.Get(), 0, 0, &_operation->overlapped);
		}
	}

	int_t IocpEngine::DrainRioCompletions(Completion* _out, const int_t _max) noexcept
	{
		if (rioProvider == nullptr || !rioProvider->IsInitialized() || _max <= 0) return 0;

		// RIODequeueCompletion 은 폴링 API 라 IOCP 완료 통지(RioKey)를 받은 시점에 호출해야 하며,
		// 한 번에 여러 RIO 완료를 배치로 회수할 수 있다.
		RIORESULT results[MaxBatch];
		const ulong_t capacity = static_cast<ulong_t>(_max < MaxBatch ? _max : MaxBatch);
		const ulong_t dequeued = rioProvider->Table().RIODequeueCompletion(rioProvider->CompletionQueue(), results, capacity);

		int_t count = 0;
		if (dequeued != 0 && dequeued != RIO_CORRUPT_CQ)
		{
			for (ulong_t i = 0; i < dequeued; ++i)
			{
				const longlong_t result = results[i].Status == 0 ?
											static_cast<longlong_t>(results[i].BytesTransferred) :
											-static_cast<longlong_t>(::RtlNtStatusToDosError(static_cast<long_t>(results[i].Status)));

				_out[count].userData = reinterpret_cast<void_t*>(static_cast<std::uintptr_t>(results[i].RequestContext));
				_out[count].result = result;
				++count;
			}
		}

		// RIO_CQ 는 통지가 한 번 발생하면 다시 ArmNotify(RIONotify)를 호출해야 다음 완료가 또
		// IOCP 로 전달된다(one-shot 통지 모델). 여기서 재무장하지 않으면 이후 RIO 완료가 누락된다.
		(void_t)rioProvider->ArmNotify();

		return count;
	}
END_NS

#endif // _WIN32
