//
// Created by hscloud on 26. 7. 7.
//

#include "Io/Engine/Iocp/Provider/RioProvider.h"

#if defined(_WIN32)
#	include "Base/Error.h"



BEGIN_NS(ne::io)
	ne::Result<BufferHandle, IoError> RioProvider::RegisterBuffer(std::span<ne::byte_t> _region) noexcept
	{
		if (_region.empty()) return ne::Result<BufferHandle, IoError>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "empty region" });

		std::lock_guard lock(mutex);

		if (auto init = EnsureInitialized(); init.IsError()) return ne::Result<BufferHandle, IoError>::Error(std::move(init.Error()));

		// RIORegisterBuffer 는 페이지를 고정(lock)하고 커널이 접근 가능한 형태로 등록하는 비교적 무거운
		// 연산이므로, 버퍼 풀 하나당 한 번만 호출하는 것을 전제로 설계되었다.
		const RIO_BUFFERID id = table.RIORegisterBuffer(reinterpret_cast<lpstr_t>(_region.data()), static_cast<ulong_t>(_region.size()));
		if (id == RIO_INVALID_BUFFERID) return ne::Result<BufferHandle, IoError>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[RioProvider/RIORegisterBuffer]"));

		// RIO_BUF 는 절대 주소가 아니라 (BufferId, Offset) 로 데이터를 가리키므로, 이후 Submit 에서
		// 사용자 포인터를 오프셋으로 환산할 수 있도록 등록 영역의 시작 주소를 기억해 둔다.
		const auto handle = BufferHandle{ reinterpret_cast<uint64_t>(id) };
		regionBases[handle.value] = _region.data();
		return ne::Result<BufferHandle, IoError>::Ok(handle);
	}

	void_t RioProvider::UnregisterBuffer(const BufferHandle _handle) noexcept
	{
		if (!_handle.IsValid()) return;

		std::lock_guard lock(mutex);
		regionBases.erase(_handle.value);
		if (!isInitialized || !table.RIODeregisterBuffer) return;


		table.RIODeregisterBuffer(reinterpret_cast<RIO_BUFFERID>(_handle.value));
	}



	void_t RioProvider::ReleaseSocket(const socket_t _socket) noexcept
	{
		std::lock_guard lock(mutex);
		requestQueues.erase(_socket);
	}



	ne::Result<void_t, IoError> RioProvider::EnsureInitialized() noexcept
	{
		if (isInitialized) return ne::Result<void_t, IoError>::Ok();

		// RIO 함수 테이블은 특정 소켓 인스턴스가 아니라 WSAIoctl 을 통해 얻는 것이라, 실제로 쓸
		// 소켓일 필요는 없다. 함수 포인터만 얻으면 되므로 임시 소켓을 만들었다가 즉시 닫는다.
		const SOCKET socket = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_REGISTERED_IO);
		if (socket == INVALID_SOCKET) return ne::Result<void_t, IoError>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[RioProvider/WSASocketW]"));

		GUID guid = WSAID_MULTIPLE_RIO;
		ulong_t bytes = 0;
		table.cbSize = sizeof(table);
		const int result = ::WSAIoctl(socket, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTERS, &guid, sizeof(guid), &table, sizeof(table), &bytes, nullptr, nullptr);
		::closesocket(socket);

		if (result == SOCKET_ERROR || !table.RIORegisterBuffer || !table.RIOCreateCompletionQueue) return ne::Result<void_t, IoError>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[RioProvider/WSAIoctl(RIO)]"));

		// RIO_CQ 자체 완료 통지를 IOCP 로 받도록 바인딩한다. RioKey 를 완료 키로 지정해 IocpEngine 이
		// 일반 OVERLAPPED 완료와 RIO 완료를 구분할 수 있게 한다.
		notifyCompletion.Type = RIO_IOCP_COMPLETION;
		notifyCompletion.Iocp.IocpHandle = iocpHandle;
		notifyCompletion.Iocp.CompletionKey = reinterpret_cast<PVOID>(rioKey);
		notifyCompletion.Iocp.Overlapped = &notifyOverlapped;

		cq = table.RIOCreateCompletionQueue(DefaultCqSize, &notifyCompletion);
		if (cq == RIO_INVALID_CQ) return ne::Result<void_t, IoError>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[RioProvider/RIOCreateCompletionQueue]"));

		cqSize = DefaultCqSize;
		isInitialized = true;

		// RIONotify 는 one-shot 통지이므로 최초 무장(arm)을 해 둬야 첫 완료가 IOCP 로 전달된다.
		return ArmNotify();
	}

	ne::Result<RIO_RQ, IoError> RioProvider::EnsureRequestQueueLocked(const socket_t _socket) noexcept
	{
		if (const auto requestQueue = requestQueues.find(_socket); requestQueue != requestQueues.end())
			return ne::Result<RIO_RQ, IoError>::Ok(requestQueue->second);

		// 송신/수신 큐 모두 동일한 공유 RIO_CQ(cq) 로 완료를 몰아넣는다 - RIO_CQ 별로 스레드/자원을
		// 따로 두지 않고, IocpEngine 하나의 완료 루프에서 일괄 처리하기 위함이다.
		const RIO_RQ rq = table.RIOCreateRequestQueue(static_cast<SOCKET>(_socket), MaxOutstandingRecv, MaxDataBuffers, MaxOutstandingSend, MaxDataBuffers, cq, cq, nullptr);
		if (rq == RIO_INVALID_RQ) return ne::Result<RIO_RQ, IoError>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[RioProvider/RIOCreateRequestQueue]"));

		requestQueues[_socket] = rq;
		return ne::Result<RIO_RQ, IoError>::Ok(rq);
	}

	RIO_BUF RioProvider::MakeRioBufferLocked(const BufferHandle _handle, const void_t* _buffer, const std::size_t _length) const noexcept
	{
		RIO_BUF buffer{};
		if (!_handle.IsValid()) return buffer;

		const auto regionBase = regionBases.find(_handle.value);
		if (regionBase == regionBases.end()) return buffer;

		// RIO_BUF 는 절대 포인터가 아니라 (BufferId, Offset, Length) 로 데이터를 표현하므로,
		// 실제 전송할 주소를 등록 영역 시작 주소 기준 상대 오프셋으로 환산해야 한다.
		buffer.BufferId = reinterpret_cast<RIO_BUFFERID>(_handle.value);
		buffer.Offset = static_cast<ULONG>(static_cast<const ne::byte_t*>(_buffer) - regionBase->second);
		buffer.Length = static_cast<ULONG>(_length);

		return buffer;
	}

	ne::Result<void_t, IoError> RioProvider::Submit(const socket_t _socket, const BufferHandle _handle, const void_t* _buffer, const std::size_t _length, void_t* _userData, const bool_t _isSend) noexcept
	{
		std::lock_guard lock(mutex);

		const RIO_BUF buffer = MakeRioBufferLocked(_handle, _buffer, _length);
		if (!buffer.BufferId) return ne::Result<void_t, IoError>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "unregistered buffer handle" });

		if (auto init = EnsureInitialized(); init.IsError()) return ne::Result<void_t, IoError>::Error(std::move(init.Error()));

		auto rq = EnsureRequestQueueLocked(_socket);
		if (rq.IsError()) return ne::Result<void_t, IoError>::Error(std::move(rq.Error()));

		RIO_BUF localBuf = buffer;
		// _userData 를 RequestContext 로 그대로 넘겨, 완료 시 RIORESULT::RequestContext 를 통해
		// 어떤 요청이 끝났는지 그대로 복원할 수 있게 한다(IocpEngine::DrainRioCompletions 참고).
		const BOOL isOk = _isSend ? table.RIOSend(rq.Value(), &localBuf, 1, 0, reinterpret_cast<PVOID>(_userData)) : table.RIOReceive(rq.Value(), &localBuf, 1, 0, reinterpret_cast<PVOID>(_userData));
		if (!isOk) return ne::Result<void_t, IoError>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context(_isSend ? "[RioProvider/RIOSend]" : "[RioProvider/RIOReceive]"));

		return ne::Result<void_t, IoError>::Ok();
	}



	ne::Result<void_t, IoError> RioProvider::ArmNotify() noexcept
	{
		if (!isInitialized || !table.RIONotify) return ne::Result<void_t, IoError>::Error(IoError{ IoErrorKind::UNSUPPORTED, "RIO CQ not initialized" });

		if (const int result = table.RIONotify(cq); result != 0)
			return ne::Result<void_t, IoError>::Error(IoError{ ne::OsError{ static_cast<ne::ulong_t>(result) } }.Context("[RioProvider/RIONotify]"));

		return ne::Result<void_t, IoError>::Ok();
	}

END_NS

#endif // _WIN32
