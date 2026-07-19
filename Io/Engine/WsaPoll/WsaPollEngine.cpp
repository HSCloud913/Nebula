//
// Created by hscloud on 26. 7. 8.
//

#include "Io/Engine/WsaPoll/WsaPollEngine.h"

#if defined(_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>



namespace
{
	// Windows 에는 POSIX eventfd/socketpair 가 없으므로, 루프백(127.0.0.1)에 스스로 connect 하는
	// TCP 소켓 쌍을 만들어 그 역할(WSAPoll 로 감시 가능한 "깨우기용" 통신 채널)을 대신한다.
	[[nodiscard]] ne::bool_t CreateWakePair(ne::io::socket_t& _readSocket, ne::io::socket_t& _writeSocket) noexcept
	{
		const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listener == INVALID_SOCKET) return false;

		// 포트 0으로 바인드해 OS 가 임의의 빈 포트를 골라주게 하고, getsockname 으로 실제 배정된
		// 포트를 읽어와 이후 connect 에 사용한다.
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
		address.sin_port = 0;

		ne::int_t addressLength = static_cast<ne::int_t>(sizeof(address));
		if (::bind(listener, reinterpret_cast<sockaddr*>(&address), addressLength) == SOCKET_ERROR || ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressLength) == SOCKET_ERROR || ::listen(listener, 1) == SOCKET_ERROR)
		{
			::closesocket(listener);
			return false;
		}

		const SOCKET writer = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (writer == INVALID_SOCKET)
		{
			::closesocket(listener);
			return false;
		}

		if (::connect(writer, reinterpret_cast<sockaddr*>(&address), addressLength) == SOCKET_ERROR)
		{
			::closesocket(listener);
			::closesocket(writer);
			return false;
		}

		const SOCKET reader = ::accept(listener, nullptr, nullptr);
		::closesocket(listener);
		if (reader == INVALID_SOCKET)
		{
			::closesocket(writer);
			return false;
		}

		// 두 소켓 모두 non-blocking 으로 전환해, Wake() 의 send() 나 신호 소비용 recv() 가
		// 절대 블로킹하지 않도록 한다.
		u_long nonBlocking = 1;
		::ioctlsocket(reader, FIONBIO, &nonBlocking);
		::ioctlsocket(writer, FIONBIO, &nonBlocking);

		_readSocket = static_cast<ne::io::socket_t>(reader);
		_writeSocket = static_cast<ne::io::socket_t>(writer);

		return true;
	}
}



BEGIN_NS(ne::io)
	WsaPollEngine::WsaPollEngine() noexcept
	{
		// EpollEngine 의 eventfd 에 대응하는 Windows 대체 수단으로 루프백 소켓 쌍을 만든다.
		socket_t readSocket = InvalidSocket;
		socket_t writeSocket = InvalidSocket;
		if (!CreateWakePair(readSocket, writeSocket)) return;

		wakeReadSocket = static_cast<ulonglong_t>(readSocket);
		wakeWriteSocket = static_cast<ulonglong_t>(writeSocket);
		isValid = true;
	}

	WsaPollEngine::~WsaPollEngine()
	{
		if (wakeReadSocket != 0) ::closesocket(static_cast<SOCKET>(wakeReadSocket));
		if (wakeWriteSocket != 0) ::closesocket(static_cast<SOCKET>(wakeWriteSocket));
	}



	void_t WsaPollEngine::Submit(const Request& _request)
	{
		// EpollEngine 과 동일한 전략: 가능하면 WSAPoll 을 거치지 않고 즉시 완료를 시도해 지연을 줄인다.
		if (longlong_t result = 0; Perform(_request, false, result))
		{
			std::lock_guard lock(mutex);
			ready.push_back(Completion{ _request.userData, result });
			return;
		}

		// WSAEWOULDBLOCK 등으로 당장 처리할 수 없는 소켓 요청만 pending 에 등록해 WSAPoll 대기로 넘긴다.
		const ulonglong_t fd = _request.handle;
		const bool_t isWrite = IsWriteDirection(_request.requestKind);

		std::lock_guard lock(mutex);

		pending[_request.userData] = PendingOperation{ _request, isWrite };
		if (isWrite) writeWaiter[fd] = _request.userData;
		else readWaiter[fd] = _request.userData;
	}

	int_t WsaPollEngine::WaitCompletions(Completion* _out, const int_t _max, const std::chrono::milliseconds _timeout)
	{
		if (_max <= 0) return 0;

		// 이전에 예약된 취소를 먼저 합성 완료로 만든다.
		ProcessCancels();

		{
			std::lock_guard lock(mutex);

			// Submit() 이 즉시 처리해 둔 결과가 있으면 WSAPoll 대기 없이 그대로 반환한다.
			int_t drained = 0;
			while (drained < _max && !ready.empty())
			{
				_out[drained++] = ready.back();
				ready.pop_back();
			}

			if (drained > 0) return drained;
		}

		// WSAPoll 은 매 호출마다 감시 대상 fd 배열을 새로 구성해 넘겨야 하므로, readWaiter/writeWaiter
		// 를 하나의 소켓당 하나의 엔트리(read+write 방향을 OR)로 병합해 fds 배열을 만든다.
		std::vector<WSAPOLLFD> fds;
		std::vector<ulonglong_t> fdOrder;
		{
			std::lock_guard lock(mutex);

			// Wake() 신호를 받을 소켓을 항상 첫 번째 엔트리로 포함시킨다.
			WSAPOLLFD wakeEntry{};
			wakeEntry.fd = static_cast<SOCKET>(wakeReadSocket);
			wakeEntry.events = POLLRDNORM;

			fds.push_back(wakeEntry);
			fdOrder.push_back(wakeReadSocket);

			std::unordered_map<ulonglong_t, SHORT> combined;
			for (const auto& [fd, userData] : readWaiter) combined[fd] |= POLLRDNORM;
			for (const auto& [fd, userData] : writeWaiter) combined[fd] |= POLLWRNORM;

			for (const auto& [fd, events] : combined)
			{
				WSAPOLLFD entry{};
				entry.fd = static_cast<SOCKET>(fd);
				entry.events = events;

				fds.push_back(entry);
				fdOrder.push_back(fd);
			}
		}

		const int_t timeoutMs = _timeout.count() < 0 ? -1 : static_cast<int_t>(_timeout.count());
		const int_t polled = ::WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), timeoutMs);
		if (polled <= 0) return 0;

		int_t count = 0;
		for (std::size_t i = 0; i < fds.size() && count < _max; ++i)
		{
			if (fds[i].revents == 0) continue;

			if (fdOrder[i] == wakeReadSocket)
			{
				// Wake 신호 자체는 완료로 노출하지 않고, 쌓여 있는 더미 바이트를 모두 비운다.
				char_t buffer[64];
				while (::recv(static_cast<SOCKET>(wakeReadSocket), buffer, sizeof(buffer), 0) > 0) {}
				continue;
			}

			const ulonglong_t fd = fdOrder[i];
			for (int_t direction = 0; direction < 2 && count < _max; ++direction)
			{
				const bool_t isWrite = (direction == 1);
				const SHORT mask = isWrite ? (POLLWRNORM | POLLERR | POLLHUP) : (POLLRDNORM | POLLERR | POLLHUP);
				if (!(fds[i].revents & mask)) continue;

				PendingOperation operation;
				bool_t isFound = false;
				{
					std::lock_guard lock(mutex);

					auto& waiter = isWrite ? writeWaiter : readWaiter;
					const auto iterator = waiter.find(fd);
					if (iterator == waiter.end()) continue;

					void_t* userData = iterator->second;
					const auto pendingIterator = pending.find(userData);
					if (pendingIterator == pending.end())
					{
						waiter.erase(iterator);
						continue;
					}

					operation = pendingIterator->second;
					isFound = true;
				}
				if (!isFound) continue;

				longlong_t result = 0;
				if (!Perform(operation.request, true, result)) continue;

				{
					std::lock_guard lock(mutex);
					(isWrite ? writeWaiter : readWaiter).erase(fd);
					pending.erase(operation.request.userData);
				}

				_out[count].userData = operation.request.userData;
				_out[count].result = result;
				++count;
			}
		}

		return count;
	}

	void_t WsaPollEngine::Wake()
	{
		// 소켓에 1바이트를 보내면 반대편(wakeReadSocket)에 POLLRDNORM 이 발생해 WSAPoll 이 깨어난다.
		constexpr char_t one = 0;
		(void_t)::send(wakeWriteSocket, &one, 1, 0);
	}

	void_t WsaPollEngine::Cancel(void_t* _userData) noexcept
	{
		if (_userData == nullptr) return;

		// 실제 취소 처리는 WaitCompletions() 시작 시점의 ProcessCancels() 에서 일괄 수행한다.
		{
			std::lock_guard lock(mutex);
			pendingCancels.push_back(_userData);
		}

		Wake();
	}

	bool_t WsaPollEngine::Supports(const Capability _capability) const noexcept
	{
		switch (_capability)
		{
			case Capability::SEND_FILE_ZERO_COPY:
				return true;
			case Capability::SEND_MEM_ZERO_COPY:
				return false;
			case Capability::RECEIVE_OVERHEAD_REDUCED:
				return false;
			case Capability::RECEIVE_TRUE_ZERO_COPY:
				return false;
		}

		return false;
	}



	bool_t WsaPollEngine::Perform(const Request& _request, const bool_t _isRetry, longlong_t& _result) noexcept
	{
		// 파일 핸들은 WSAPoll 로 감시할 수 없는 대상이므로(WSAPoll 은 소켓 전용 API), 체인 Read/Write
		// 는 항상 여기서 OVERLAPPED + GetOverlappedResult(TRUE) 로 동기적으로 끝까지 완료시킨다.
		if (_request.chain != nullptr && (_request.requestKind == RequestKind::READ || _request.requestKind == RequestKind::WRITE))
		{
			const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(_request.handle));
			longlong_t total = 0;
			ulonglong_t currentOffset = _request.offset;

			for (const auto& segment : _request.chain->Segments())
			{
				OVERLAPPED overlapped{};
				overlapped.Offset = static_cast<ulong_t>(currentOffset & 0xFFFFFFFFull);
				overlapped.OffsetHigh = static_cast<ulong_t>(currentOffset >> 32);

				ulong_t transferred = 0;
				bool_t isOk = (_request.requestKind == RequestKind::READ) ?
								::ReadFile(handle, segment.ptr, static_cast<ulong_t>(segment.length), &transferred, &overlapped) :
								::WriteFile(handle, segment.ptr, static_cast<ulong_t>(segment.length), &transferred, &overlapped);
				if (!isOk && ::GetLastError() == ERROR_IO_PENDING) { isOk = ::GetOverlappedResult(handle, &overlapped, &transferred, TRUE); }

				if (!isOk)
				{
					_result = -static_cast<longlong_t>(::GetLastError());
					return true;
				}

				total += static_cast<longlong_t>(transferred);
				currentOffset += transferred;
				if (transferred < segment.length) break;
			}

			_result = total;

			return true;
		}

		switch (_request.requestKind)
		{
			case RequestKind::ACCEPT:
			{
				const SOCKET socket = _request.handle;

				const SOCKET accepted = ::accept(socket, nullptr, nullptr);
				if (accepted != INVALID_SOCKET)
				{
					_result = static_cast<longlong_t>(accepted);
					return true;
				}
				break;
			}
			case RequestKind::CONNECT:
			{
				// non-blocking connect() 는 WSAEWOULDBLOCK 을 반환하며 진행되고, 실제 성공 여부는
				// 소켓이 쓰기 가능(POLLWRNORM) 해진 뒤 SO_ERROR 로 확인해야 한다(EpollEngine 과 동일한 패턴).
				const SOCKET socket = _request.handle;

				if (_isRetry)
				{
					int_t soError = 0;
					int_t length = static_cast<int_t>(sizeof(soError));
					(void_t)::getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<lpstr_t>(&soError), &length);
					_result = (soError == 0) ? 0 : -static_cast<longlong_t>(soError);
					return true;
				}

				if (::connect(socket, static_cast<const sockaddr*>(_request.address), _request.addressLength) == 0)
				{
					_result = 0;
					return true;
				}

				if (::WSAGetLastError() == WSAEWOULDBLOCK) return false;
				break;
			}
			case RequestKind::READ:
			{
				// 파일 핸들은 WSAPoll 대상이 될 수 없으므로 이 op 도 항상 즉시 동기 완료된다
				// (ERROR_IO_PENDING 이어도 GetOverlappedResult(TRUE) 로 여기서 끝까지 기다린다).
				const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(_request.handle));

				OVERLAPPED overlapped{};
				overlapped.Offset = static_cast<ulong_t>(_request.offset & 0xFFFFFFFFull);
				overlapped.OffsetHigh = static_cast<ulong_t>(_request.offset >> 32);

				ulong_t read = 0;
				if (::ReadFile(handle, _request.buffer, static_cast<ulong_t>(_request.length), &read, &overlapped))
				{
					_result = static_cast<longlong_t>(read);
					return true;
				}

				if (::GetLastError() == ERROR_IO_PENDING)
				{
					ulong_t transferred = 0;
					if (::GetOverlappedResult(handle, &overlapped, &transferred, TRUE))
					{
						_result = static_cast<longlong_t>(transferred);
						return true;
					}
				}
				break;
			}
			case RequestKind::WRITE:
			{
				const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(_request.handle));

				OVERLAPPED overlapped{};
				overlapped.Offset = static_cast<ulong_t>(_request.offset & 0xFFFFFFFFull);
				overlapped.OffsetHigh = static_cast<ulong_t>(_request.offset >> 32);

				ulong_t written = 0;
				if (::WriteFile(handle, _request.buffer, _request.length, &written, &overlapped))
				{
					_result = static_cast<longlong_t>(written);
					return true;
				}

				if (::GetLastError() == ERROR_IO_PENDING)
				{
					ulong_t transferred = 0;
					if (::GetOverlappedResult(handle, &overlapped, &transferred, TRUE))
					{
						_result = static_cast<longlong_t>(transferred);
						return true;
					}
				}
				break;
			}
			case RequestKind::READ_FIXED:
			{
				const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(_request.handle));

				OVERLAPPED overlapped{};
				overlapped.Offset = static_cast<ulong_t>(_request.offset & 0xFFFFFFFFull);
				overlapped.OffsetHigh = static_cast<ulong_t>(_request.offset >> 32);

				ulong_t read = 0;
				if (::ReadFile(handle, _request.buffer, static_cast<ulong_t>(_request.length), &read, &overlapped))
				{
					_result = static_cast<longlong_t>(read);
					return true;
				}

				if (::GetLastError() == ERROR_IO_PENDING)
				{
					ulong_t transferred = 0;
					if (::GetOverlappedResult(handle, &overlapped, &transferred, TRUE))
					{
						_result = static_cast<longlong_t>(transferred);
						return true;
					}
				}
				break;
			}
			case RequestKind::WRITE_FIXED:
			{
				const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(_request.handle));

				OVERLAPPED overlapped{};
				overlapped.Offset = static_cast<ulong_t>(_request.offset & 0xFFFFFFFFull);
				overlapped.OffsetHigh = static_cast<ulong_t>(_request.offset >> 32);

				ulong_t written = 0;
				if (::WriteFile(handle, _request.buffer, static_cast<ulong_t>(_request.length), &written, &overlapped))
				{
					_result = static_cast<longlong_t>(written);
					return true;
				}

				if (::GetLastError() == ERROR_IO_PENDING)
				{
					ulong_t transferred = 0;
					if (::GetOverlappedResult(handle, &overlapped, &transferred, TRUE))
					{
						_result = static_cast<longlong_t>(transferred);
						return true;
					}
				}
				break;
			}
			case RequestKind::WAIT_READABLE:
			case RequestKind::WAIT_WRITABLE:
				if (!_isRetry) return false;
				_result = 0;
				return true;

			case RequestKind::RECEIVE:
			{
				const SOCKET socket = _request.handle;

				if (_request.chain != nullptr)
				{
					// overlapped 인자를 nullptr 로 넘기므로 이 호출은 완전히 동기(blocking 여부는
					// 소켓의 non-blocking 설정에 따름) 이다 - IocpEngine 과 달리 여기서는 소켓이
					// non-blocking 이라 즉시 반환되며, 실패 시 WSAEWOULDBLOCK 으로 재시도를 유도한다.
					std::vector<WSABUF> wsaBuffers;
					wsaBuffers.reserve(_request.chain->Segments().size());
					for (const auto& segment : _request.chain->Segments()) wsaBuffers.push_back(WSABUF{ .len = static_cast<ulong_t>(segment.length), .buf = reinterpret_cast<lpstr_t>(segment.ptr) });

					ulong_t received = 0;
					ulong_t flags = 0;
					if (::WSARecv(socket, wsaBuffers.data(), static_cast<ulong_t>(wsaBuffers.size()), &received, &flags, nullptr, nullptr) == 0)
					{
						_result = static_cast<longlong_t>(received);
						return true;
					}
					break;
				}

				const int_t bytes = ::recv(socket, static_cast<lpstr_t>(_request.buffer), static_cast<int_t>(_request.length), 0);
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case RequestKind::SEND:
			{
				const SOCKET socket = _request.handle;

				if (_request.chain != nullptr)
				{
					std::vector<WSABUF> wsaBuffers;
					wsaBuffers.reserve(_request.chain->Segments().size());
					for (const auto& segment : _request.chain->Segments()) wsaBuffers.push_back(WSABUF{ .len = static_cast<ulong_t>(segment.length), .buf = reinterpret_cast<lpstr_t>(segment.ptr) });

					ulong_t sent = 0;
					if (::WSASend(socket, wsaBuffers.data(), static_cast<ulong_t>(wsaBuffers.size()), &sent, 0, nullptr, nullptr) == 0)
					{
						_result = static_cast<longlong_t>(sent);
						return true;
					}
					break;
				}

				const int_t bytes = ::send(socket, static_cast<lpstr_t>(_request.buffer), static_cast<int_t>(_request.length), 0);
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case RequestKind::RECEIVE_FROM:
			{
				const SOCKET socket = _request.handle;
				const int_t bytes = ::recvfrom(socket,
												static_cast<lpstr_t>(_request.buffer),
												static_cast<int_t>(_request.length),
												0,
												static_cast<sockaddr*>(_request.fromAddress),
												_request.fromAddressLength);
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case RequestKind::SEND_TO:
			{
				const SOCKET socket = _request.handle;
				const int_t bytes = ::sendto(socket,
											static_cast<lpstr_t>(_request.buffer),
											static_cast<int_t>(_request.length),
											0,
											static_cast<const sockaddr*>(_request.address),
											_request.addressLength);
				if (bytes >= 0)
				{
					_result = static_cast<longlong_t>(bytes);
					return true;
				}
				break;
			}
			case RequestKind::SEND_FILE:
			{
				// TransmitFile 도 OVERLAPPED 를 받을 수 있으나, 여기서는 non-blocking 소켓에 대해
				// WSA_IO_PENDING 이 나오면 곧바로 GetOverlappedResult(TRUE) 로 완료까지 기다려
				// WSAPoll 을 거치지 않고 동기적으로 확정한다.
				const SOCKET destSocket = static_cast<SOCKET>(_request.handle);
				const HANDLE sourceFile = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(_request.auxHandle));

				OVERLAPPED overlapped{};
				overlapped.Offset = static_cast<ulong_t>(_request.offset & 0xFFFFFFFFull);
				overlapped.OffsetHigh = static_cast<ulong_t>(_request.offset >> 32);

				if (::TransmitFile(destSocket, sourceFile, static_cast<ulong_t>(_request.length), 0, &overlapped, nullptr, 0))
				{
					_result = static_cast<longlong_t>(_request.length);
					return true;
				}

				if (::WSAGetLastError() == WSA_IO_PENDING)
				{
					ulong_t transferred = 0;
					if (::GetOverlappedResult(reinterpret_cast<HANDLE>(destSocket), &overlapped, &transferred, TRUE))
					{
						_result = static_cast<longlong_t>(transferred);
						return true;
					}
				}
				break;
			}
			default:
				_result = -static_cast<longlong_t>(ERROR_NOT_SUPPORTED);
				return true;
		}

		const int_t error = ::WSAGetLastError();
		if (error == WSAEWOULDBLOCK) return false;

		_result = -static_cast<longlong_t>(error);

		return true;
	}

	bool_t WsaPollEngine::IsWriteDirection(const RequestKind _requestKind) noexcept
	{
		return _requestKind == RequestKind::WRITE ||
				_requestKind == RequestKind::SEND ||
				_requestKind == RequestKind::CONNECT ||
				_requestKind == RequestKind::WRITE_FIXED ||
				_requestKind == RequestKind::SEND_FILE ||
				_requestKind == RequestKind::SEND_TO ||
				_requestKind == RequestKind::WAIT_WRITABLE;
	}

	void_t WsaPollEngine::ProcessCancels()
	{
		// mutex 를 오래 잡지 않도록 pendingCancels 를 통째로 스왑해 락 밖에서 순회한다.
		std::vector<void_t*> cancels;
		{
			std::lock_guard lock(mutex);
			cancels.swap(pendingCancels);
		}

		for (void_t* userData : cancels)
		{
			std::lock_guard lock(mutex);
			const auto iterator = pending.find(userData);
			if (iterator == pending.end()) continue; // 이미 완료되었거나 존재하지 않는 요청은 무시.

			const ulonglong_t fd = iterator->second.request.handle;
			if (iterator->second.isWrite) writeWaiter.erase(fd);
			else readWaiter.erase(fd);
			pending.erase(iterator);

			// 취소를 합성 완료(ERROR_OPERATION_ABORTED)로 변환해 다음 WaitCompletions() 에서 통지되게 한다.
			ready.push_back(Completion{ userData, -static_cast<longlong_t>(ERROR_OPERATION_ABORTED) });
		}
	}

END_NS

#endif // _WIN32
