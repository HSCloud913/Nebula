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

		if (auto init = EnsureInitialized(); init.IsError())
			return ne::Result<BufferHandle, IoError>::Error(std::move(init.Error()));

		const RIO_BUFFERID id = table.RIORegisterBuffer(reinterpret_cast<lpstr_t>(_region.data()), static_cast<ulong_t>(_region.size()));
		if (id == RIO_INVALID_BUFFERID)
			return ne::Result<BufferHandle, IoError>::Error(
				IoError{ ne::OsError{ ne::LastOsError() } }.Context("[RioProvider/RIORegisterBuffer]"));

		// RIO_BUFFERID(불투명 핸들)를 그대로 저장. 유효 핸들은 0(무효 값)이 되지 않는다.
		const auto handle = BufferHandle{ reinterpret_cast<uint64_t>(id) };
		regionBases[handle.value] = _region.data(); // Submit() 에서 넘어온 sub-range 포인터의 Offset 산정 기준
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
		requestQueues.erase(_socket); // RIO_RQ 는 소켓 close 로 OS 가 파괴 — 맵 엔트리만 지운다.
	}



	ne::Result<void_t, IoError> RioProvider::EnsureInitialized() noexcept
	{
		if (isInitialized) return ne::Result<void_t, IoError>::Ok();

		// 1) RIO 확장 테이블 획득 — RIO 소켓에서 WSAIoctl 로 함수 포인터를 받는다(프로세스 전역).
		const SOCKET tmp = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
										WSA_FLAG_OVERLAPPED | WSA_FLAG_REGISTERED_IO);
		if (tmp == INVALID_SOCKET)
			return ne::Result<void_t, IoError>::Error(
				IoError{ ne::OsError{ ne::LastOsError() } }.Context("[RioProvider/WSASocketW]"));

		GUID guid = WSAID_MULTIPLE_RIO;
		ulong_t bytes = 0;
		table.cbSize = sizeof(table);
		const int rc = ::WSAIoctl(tmp, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTERS,
								&guid, sizeof(guid), &table, sizeof(table), &bytes, nullptr, nullptr);
		::closesocket(tmp);

		if (rc == SOCKET_ERROR || !table.RIORegisterBuffer || !table.RIOCreateCompletionQueue)
			return ne::Result<void_t, IoError>::Error(
				IoError{ ne::OsError{ ne::LastOsError() } }.Context("[RioProvider/WSAIoctl(RIO)]"));

		// 2) 공유 RIO_CQ 생성 — 완료를 엔진 IOCP 로 통지(RIO_IOCP_COMPLETION). notifyOverlapped 는
		//    RIONotify 완료가 실릴 OVERLAPPED 로, provider 수명 내내 안정적으로 유지돼야 한다.
		notifyCompletion.Type = RIO_IOCP_COMPLETION;
		notifyCompletion.Iocp.IocpHandle = iocp;
		notifyCompletion.Iocp.CompletionKey = reinterpret_cast<PVOID>(rioKey);
		notifyCompletion.Iocp.Overlapped = &notifyOverlapped;

		cq = table.RIOCreateCompletionQueue(DefaultCqSize, &notifyCompletion);
		if (cq == RIO_INVALID_CQ)
			return ne::Result<void_t, IoError>::Error(
				IoError{ ne::OsError{ ne::LastOsError() } }.Context("[RioProvider/RIOCreateCompletionQueue]"));

		cqSize = DefaultCqSize;
		isInitialized = true;

		// 최초 통지 무장 — 이후 첫 RIO 완료가 IOCP 를 깨우게 한다(RIONotify 는 one-shot).
		return ArmNotify();
	}

	ne::Result<RIO_RQ, IoError> RioProvider::EnsureRequestQueueLocked(const socket_t _socket) noexcept
	{
		if (const auto it = requestQueues.find(_socket); it != requestQueues.end()) return ne::Result<RIO_RQ, IoError>::Ok(it->second);

		// recv/send 모두 같은 공유 CQ 로 완료를 받는다. SocketContext 는 안 쓰고(per-request context 사용) nullptr.
		const RIO_RQ rq = table.RIOCreateRequestQueue(
			static_cast<SOCKET>(_socket),
			MaxOutstandingRecv, MaxDataBuffers,
			MaxOutstandingSend, MaxDataBuffers,
			cq, cq, nullptr);
		if (rq == RIO_INVALID_RQ)
			return ne::Result<RIO_RQ, IoError>::Error(
				IoError{ ne::OsError{ ne::LastOsError() } }.Context("[RioProvider/RIOCreateRequestQueue]"));

		requestQueues[_socket] = rq;
		return ne::Result<RIO_RQ, IoError>::Ok(rq);
	}

	RIO_BUF RioProvider::MakeRioBufferLocked(const BufferHandle _handle, const void_t* _buffer, const std::size_t _length) const noexcept
	{
		RIO_BUF buffer{}; // BufferId 기본 nullptr(0) = 무효
		if (!_handle.IsValid()) return buffer;

		const auto it = regionBases.find(_handle.value);
		if (it == regionBases.end()) return buffer; // RegisterBuffer 로 등록된 적 없는 handle

		buffer.BufferId = reinterpret_cast<RIO_BUFFERID>(_handle.value);
		buffer.Offset = static_cast<ULONG>(static_cast<const ne::byte_t*>(_buffer) - it->second);
		buffer.Length = static_cast<ULONG>(_length);

		return buffer;
	}

	ne::Result<void_t, IoError> RioProvider::Submit(const socket_t _socket, const BufferHandle _handle, const void_t* _buffer, const std::size_t _length, void_t* _userData, const bool_t _isSend) noexcept
	{
		std::lock_guard lock(mutex); // EnsureInitialized/RQ 조회·생성/RIO 제출/regionBases 조회를 함께 보호(엔진은 MT RunOnce)

		const RIO_BUF buf = MakeRioBufferLocked(_handle, _buffer, _length);
		if (!buf.BufferId) return ne::Result<void_t, IoError>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "unregistered buffer handle" });

		if (auto init = EnsureInitialized(); init.IsError()) return ne::Result<void_t, IoError>::Error(std::move(init.Error()));

		auto rq = EnsureRequestQueueLocked(_socket);
		if (rq.IsError()) return ne::Result<void_t, IoError>::Error(std::move(rq.Error()));

		// RequestContext = userData (완료 시 IocpEngine::DrainRioCompletions 가 Completion{userData,...}
		// 로 그대로 정규화). 단일 RIO_BUF.
		RIO_BUF localBuf = buf; // RIO 는 제출 시 내용을 복사하므로 지역 변수로 넘겨도 안전
		const BOOL ok = _isSend
							? table.RIOSend(rq.Value(), &localBuf, 1, 0, reinterpret_cast<PVOID>(_userData))
							: table.RIOReceive(rq.Value(), &localBuf, 1, 0, reinterpret_cast<PVOID>(_userData));
		if (!ok)
			return ne::Result<void_t, IoError>::Error(
				IoError{ ne::OsError{ ne::LastOsError() } }.Context(_isSend ? "[RioProvider/RIOSend]" : "[RioProvider/RIOReceive]"));

		return ne::Result<void_t, IoError>::Ok();
	}



	ne::Result<void_t, IoError> RioProvider::ArmNotify() noexcept
	{
		// 드레인 스레드(단일) 또는 EnsureInitialized(mutex 보유) 에서만 호출된다 — 별도 락 없음.
		if (!isInitialized || !table.RIONotify) return ne::Result<void_t, IoError>::Error(IoError{ IoErrorKind::UNSUPPORTED, "RIO CQ not initialized" });

		if (const int rc = table.RIONotify(cq); rc != 0) // 0 = ERROR_SUCCESS
			return ne::Result<void_t, IoError>::Error(
				IoError{ ne::OsError{ static_cast<ne::ulong_t>(rc) } }.Context("[RioProvider/RIONotify]"));

		return ne::Result<void_t, IoError>::Ok();
	}

END_NS

#endif // _WIN32
