//
// Created by hscloud on 26. 7. 8.
//

#include "Io/Engine/WsaPoll/WsaPollEngine.h"

#if defined(_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

BEGIN_NS(ne::io)
	namespace
	{
		// Windows 에는 socketpair()/eventfd 가 없다 — 로컬 루프백 TCP 연결로 대체해 Wake() 용
		// 읽기/쓰기 소켓 쌍을 만든다.
		[[nodiscard]] bool_t CreateWakePair(socket_t& _readSocket, socket_t& _writeSocket) noexcept
		{
			const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (listener == INVALID_SOCKET) return false;

			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
			address.sin_port = 0;

			int_t addressLength = static_cast<int_t>(sizeof(address));
			if (::bind(listener, reinterpret_cast<sockaddr*>(&address), addressLength) == SOCKET_ERROR ||
				::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressLength) == SOCKET_ERROR ||
				::listen(listener, 1) == SOCKET_ERROR)
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

			// Wake() 의 send()/드레인의 recv() 모두 블록하면 안 되므로 논블로킹으로 전환.
			u_long nonBlocking = 1;
			::ioctlsocket(reader, FIONBIO, &nonBlocking);
			::ioctlsocket(writer, FIONBIO, &nonBlocking);

			_readSocket = static_cast<socket_t>(reader);
			_writeSocket = static_cast<socket_t>(writer);
			return true;
		}
	}

	WsaPollEngine::WsaPollEngine() noexcept
	{
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
		if (longlong_t result = 0; Perform(_request, false, result))
		{
			std::lock_guard lock(mutex);
			ready.push_back(Completion{ _request.userData, result });
			return;
		}

		// WSAEWOULDBLOCK/연결 진행 중 — WSAPoll 에 등록하고 준비되면 재수행.
		const ulonglong_t fd = _request.handle;
		const bool_t isWrite = IsWriteDirection(_request.op);

		std::lock_guard lock(mutex);

		pending[_request.userData] = PendingOperation{ _request, isWrite };
		if (isWrite) writeWaiter[fd] = _request.userData;
		else readWaiter[fd] = _request.userData;
	}

	int_t WsaPollEngine::WaitCompletions(Completion* _out, const int_t _max, const std::chrono::milliseconds _timeout)
	{
		if (_max <= 0) return 0;

		ProcessCancels();

		// 합성 완료(즉시 완료/취소) 우선 배출.
		{
			std::lock_guard lock(mutex);

			int_t drained = 0;
			while (drained < _max && !ready.empty())
			{
				_out[drained++] = ready.back();
				ready.pop_back();
			}

			if (drained > 0) return drained;
		}

		// WSAPoll 은 epoll 과 달리 영속 등록이 없다 — 매 호출마다 현재 대기 목록으로 fd 배열을 새로 구성한다.
		std::vector<WSAPOLLFD> fds;
		std::vector<ulonglong_t> fdOrder;
		{
			std::lock_guard lock(mutex);

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
		if (polled <= 0) return 0; // 타임아웃/오류

		int_t count = 0;
		for (std::size_t i = 0; i < fds.size() && count < _max; ++i)
		{
			if (fds[i].revents == 0) continue;

			if (fdOrder[i] == wakeReadSocket)
			{
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
				if (!Perform(operation.request, true, result)) continue; // 아직 WSAEWOULDBLOCK — 계속 대기

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
		const char_t one = 0;
		(void_t)::send(static_cast<SOCKET>(wakeWriteSocket), &one, 1, 0);
	}

	void_t WsaPollEngine::Cancel(void_t* _userData) noexcept
	{
		if (_userData == nullptr) return;

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
		case Capability::SendFileZeroCopy: return true;  // TransmitFile(SendFile) — RIO 없이도 가능
		case Capability::SendMemZeroCopy: return false; // RIO 는 IOCP 완료 모델 전제라 reactor 폴백에서 불가
		case Capability::RecvOverheadReduced: return false; // 등록 버퍼 없음
		case Capability::RecvTrueZeroCopy: return false;
		}

		return false;
	}



	bool_t WsaPollEngine::Perform(const Request& _request, const bool_t _isRetry, longlong_t& _result) noexcept
	{
		// 파일 scatter/gather — IocpEngine 과 동일한 이유(임의 크기 세그먼트는 ReadFileScatter/
		// WriteFileGather 의 페이지 정렬 요구를 못 맞춤)로 세그먼트별 순차 동기 처리 후 합산한다.
		if (_request.chain != nullptr && (_request.op == OpCode::Read || _request.op == OpCode::Write))
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
				bool_t isOk = (_request.op == OpCode::Read)
								? ::ReadFile(handle, segment.ptr, static_cast<ulong_t>(segment.length), &transferred, &overlapped)
								: ::WriteFile(handle, segment.ptr, static_cast<ulong_t>(segment.length), &transferred, &overlapped);
				if (!isOk && ::GetLastError() == ERROR_IO_PENDING)
				{
					isOk = ::GetOverlappedResult(handle, &overlapped, &transferred, TRUE);
				}

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

		switch (_request.op)
		{
		case OpCode::Read:
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
		case OpCode::Write:
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
		case OpCode::Receive:
		{
			const SOCKET socket = _request.handle;

			if (_request.chain != nullptr)
			{
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
		case OpCode::Send:
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
		case OpCode::SendTo: // 비연결형(UDP) 송신 — address/addressLength 가 매 호출 목적지
		{
			const SOCKET socket = _request.handle;
			const int_t bytes = ::sendto(socket, static_cast<lpstr_t>(_request.buffer), static_cast<int_t>(_request.length), 0,
			                              static_cast<const sockaddr*>(_request.address), _request.addressLength);
			if (bytes >= 0) { _result = static_cast<longlong_t>(bytes); return true; }
			break;
		}
		case OpCode::ReceiveFrom: // 비연결형(UDP) 수신 — fromAddress/fromAddressLength 에 발신자 주소를 채움
		{
			const SOCKET socket = _request.handle;
			const int_t bytes = ::recvfrom(socket, static_cast<lpstr_t>(_request.buffer), static_cast<int_t>(_request.length), 0,
			                                static_cast<sockaddr*>(_request.fromAddress), _request.fromAddressLength);
			if (bytes >= 0) { _result = static_cast<longlong_t>(bytes); return true; }
			break;
		}
		case OpCode::Accept:
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
		case OpCode::Connect:
		{
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
				return true; // 즉시 연결(로컬 등)
			}

			if (::WSAGetLastError() == WSAEWOULDBLOCK) return false; // POLLOUT 대기
			break;
		}
		case OpCode::SendFile: // handle=목적지 소켓(Send 계열과 동일), auxHandle=원본 파일.
		{                      // IocpEngine 과 달리 여기엔 IOCP 가 없어 stray completion 위험이 없으므로
			// 로컬 OVERLAPPED 로 바로 동기 완료시킬 수 있다(File Read/Write 와 동일 패턴).
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
		case OpCode::ReadFixed: // Windows 에 파일 등록 버퍼 개념이 없다 — 일반 Read 와 동일, bufferId 무시
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
		case OpCode::WriteFixed: // 위와 동일
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
		default:
			_result = -static_cast<longlong_t>(ERROR_NOT_SUPPORTED); // SendZeroCopy(RIO) 는 reactor 모델에서 불가
			return true;
		}

		const int_t error = ::WSAGetLastError();
		if (error == WSAEWOULDBLOCK) return false; // WSAPoll 대기 필요

		_result = -static_cast<longlong_t>(error);

		return true;
	}

	bool_t WsaPollEngine::IsWriteDirection(const OpCode _op) noexcept
	{
		return _op == OpCode::Write || _op == OpCode::Send || _op == OpCode::Connect
				|| _op == OpCode::WriteFixed || _op == OpCode::SendFile || _op == OpCode::SendTo;
	}

	void_t WsaPollEngine::ProcessCancels()
	{
		std::vector<void_t*> cancels;
		{
			std::lock_guard lock(mutex);
			cancels.swap(pendingCancels);
		}

		for (void_t* userData : cancels)
		{
			std::lock_guard lock(mutex);
			const auto iterator = pending.find(userData);
			if (iterator == pending.end()) continue;

			const ulonglong_t fd = iterator->second.request.handle;
			if (iterator->second.isWrite) writeWaiter.erase(fd);
			else readWaiter.erase(fd);
			pending.erase(iterator);

			// IocpEngine 의 실제 취소(CancelIoEx) 결과와 대칭되는 코드로 합성한다.
			ready.push_back(Completion{ userData, -static_cast<longlong_t>(ERROR_OPERATION_ABORTED) });
		}
	}

END_NS

#endif // _WIN32
