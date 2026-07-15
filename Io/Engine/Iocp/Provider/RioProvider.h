//
// Created by hscloud on 26. 7. 7.
//
// Winsock RIO(Registered I/O) 기반 IRegisteredBufferProvider 구현.
// IocpEngine 이 소유(합성)하며, 완료 통지는 엔진의 IOCP 핸들로 받는다(RIO_IOCP_COMPLETION).
// 별도 엔진 클래스를 만들지 않고 IOCP 엔진에 부가 기능으로 얹는다.

#pragma once

#if defined(_WIN32)
#	include <mutex>
#	include <unordered_map>
#	include "Io/Engine/IRegisteredBufferProvider.h"
#	include "Io/Engine/Iocp/Provider/RioExtension.h"
#	include "Io/IoError.h"
#	include "Io/IoType.h"
#	include "Base/Result.h"
#	include "Base/Type.h"

BEGIN_NS(ne::io)
	class RioProvider final :public IRegisteredBufferProvider
	{
	public:
		// _iocp: 완료 통지를 바인딩할 엔진의 IOCP 핸들. _rioKey: RIO 완료를 GQCS 에서 구분할 sentinel key.
		RioProvider(const HANDLE _iocpHandle, const ulonglong_t _rioKey) noexcept
			: iocpHandle(_iocpHandle)
			, rioKey(_rioKey) {}

		virtual ~RioProvider() override { if (cq != RIO_INVALID_CQ && table.RIOCloseCompletionQueue) table.RIOCloseCompletionQueue(cq); }

		NEBULA_NON_COPYABLE_MOVABLE(RioProvider)

	private:
		// RIO_CQ 크기(고정). RIOCreateCompletionQueue 는 사전 크기 산정이 필요하다 — 보수적 기본값을
		// 두고, 부족하면 RIOResizeCompletionQueue 로 키운다(추후). TODO: 워크로드 기반 산정.
		static constexpr ulong_t DefaultCqSize = 4096;

		// RIO_RQ 용량(소켓당). outstanding 요청 수 / op 당 RIO_BUF 개수(단일 버퍼 = 1).
		static constexpr ulong_t MaxOutstandingRecv = 64;
		static constexpr ulong_t MaxOutstandingSend = 64;
		static constexpr ulong_t MaxDataBuffers = 1;

	private:
		HANDLE iocpHandle{};
		ulonglong_t rioKey{};

	private:
		std::mutex mutex; // 초기화/등록/해제/RQ 생성 보호 — IocpEngine 은 멀티스레드 RunOnce 모델.
		RIO_EXTENSION_FUNCTION_TABLE table{};
		RIO_CQ cq{ RIO_INVALID_CQ };
		RIO_NOTIFICATION_COMPLETION notifyCompletion{}; // CQ 생성 시 IOCP 바인딩 정보
		OVERLAPPED notifyOverlapped{};                  // RIONotify 완료가 실릴 OVERLAPPED — 수명 내내 유지
		bool_t isInitialized{ false };
		ulong_t cqSize{ 0 };
		std::unordered_map<socket_t, RIO_RQ> requestQueues;    // socket → RIO_RQ (lazy)
		std::unordered_map<uint64_t, ne::byte_t*> regionBases; // BufferHandle.value → RegisterBuffer 에 넘겼던 region.data()(Offset 산정용)

	public: /* IRegisteredBufferProvider */
		[[nodiscard]] virtual ne::Result<BufferHandle, IoError> RegisterBuffer(std::span<ne::byte_t> _region) noexcept override;
		virtual void_t UnregisterBuffer(BufferHandle _handle) noexcept override;

	public: /* IRegisteredBufferProvider */
		[[nodiscard]] virtual ne::Result<void_t, IoError> SubmitSendRegistered(const socket_t _socket, const BufferHandle _handle, const void_t* _buffer, const std::size_t _length, void_t* _userData) noexcept override { return Submit(_socket, _handle, _buffer, _length, _userData, true); }
		[[nodiscard]] virtual ne::Result<void_t, IoError> SubmitReceiveRegistered(const socket_t _socket, const BufferHandle _handle, void_t* _buffer, const std::size_t _length, void_t* _userData) noexcept override { return Submit(_socket, _handle, _buffer, _length, _userData, false); }

	public: /* IRegisteredBufferProvider */
		virtual void_t ReleaseSocket(socket_t _socket) noexcept override; // 소켓별 RIO_RQ 맵 엔트리 제거

	private:
		// lazy: RIO 확장 테이블 획득 + 공유 RIO_CQ 생성(IOCP 바인딩) + 최초 통지 무장. mutex 를 이미 쥔 상태로 호출.
		[[nodiscard]] ne::Result<void_t, IoError> EnsureInitialized() noexcept;
		// 소켓별 RIO_RQ 를 lazy 생성해 반환한다(공유 CQ 에 recv/send 모두 연결). mutex 를 이미 쥔 상태로 호출.
		[[nodiscard]] ne::Result<RIO_RQ, IoError> EnsureRequestQueueLocked(socket_t _socket) noexcept;
		// (handle, buffer, length) → RIO_BUF. Offset 은 regionBases[handle] 기준 — 등록 안 된 handle 이면 BufferId 0(무효).
		// mutex 를 이미 쥔 상태로 호출.
		[[nodiscard]] RIO_BUF MakeRioBufferLocked(BufferHandle _handle, const void_t* _buffer, std::size_t _length) const noexcept;
		// 공통 제출 경로 — _isSend 로 RIOSend/RIOReceive 분기.
		[[nodiscard]] ne::Result<void_t, IoError> Submit(socket_t _socket, BufferHandle _handle, const void_t* _buffer, std::size_t _length, void_t* _userData, bool_t _isSend) noexcept;

	public: // RIO_CQ 완료 통지 재무장(RIONotify 는 one-shot). 완료 드레인 직후 호출한다.
		[[nodiscard]] ne::Result<void_t, IoError> ArmNotify() noexcept;

	public: // 완료 루프에서 쓰는 접근자 — 초기화 후에는 읽기 전용이라 mutex 밖 사용 가능.
		[[nodiscard]] bool_t IsInitialized() const noexcept { return isInitialized; }
		[[nodiscard]] const RIO_EXTENSION_FUNCTION_TABLE& Table() const noexcept { return table; }
		[[nodiscard]] RIO_CQ CompletionQueue() const noexcept { return cq; }
	};

END_NS

#endif // _WIN32
