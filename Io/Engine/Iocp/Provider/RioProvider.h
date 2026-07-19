//
// Created by hscloud on 26. 7. 7.
//

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
	/**
	 * @class RioProvider
	 * @brief Winsock RIO(Registered I/O) 기반 IRegisteredBufferProvider 구현.
	 *
	 * IocpEngine 이 소유(합성)하는 zero-copy 송수신 provider. 소켓별 RIO_RQ 와 공유 RIO_CQ 를
	 * lazy 생성하며, RIO_CQ 완료 통지는 별도 엔진 클래스 없이 IocpEngine 의 IOCP 핸들에
	 * RIO_IOCP_COMPLETION 으로 바인딩해 받는다. RegisterBuffer 로 등록된 영역만 SubmitSendRegistered/
	 * SubmitReceiveRegistered 로 송수신할 수 있다.
	 *
	 * @note mutex 로 초기화/등록/해제/RQ 생성을 보호하며, 멀티스레드에서 호출될 수 있다.
	 */
	class RioProvider final :public IRegisteredBufferProvider
	{
	public:
		/**
		 * @brief RIO 완료가 통지될 IOCP 핸들과 구분용 완료 키만 저장한다(RIO 자체 초기화는 지연시킨다).
		 * @param _iocpHandle RIO_CQ 통지를 바인딩할 IocpEngine 소유의 IOCP 핸들.
		 * @param _rioKey WaitCompletions() 이 일반 op 완료와 RIO 완료를 구분하기 위한 완료 키(IocpEngine::RioKey).
		 */
		RioProvider(const HANDLE _iocpHandle, const ulonglong_t _rioKey) noexcept
			: iocpHandle(_iocpHandle)
			, rioKey(_rioKey) {}

		/** @brief 생성해 둔 RIO 완료 큐(RIO_CQ)가 있으면 RIOCloseCompletionQueue 로 닫는다. */
		virtual ~RioProvider() override { if (cq != RIO_INVALID_CQ && table.RIOCloseCompletionQueue) table.RIOCloseCompletionQueue(cq); }

		NEBULA_NON_COPYABLE_MOVABLE(RioProvider)

	private:
		static constexpr ulong_t DefaultCqSize = 4096;

		static constexpr ulong_t MaxOutstandingRecv = 64;
		static constexpr ulong_t MaxOutstandingSend = 64;
		static constexpr ulong_t MaxDataBuffers = 1;

	private:
		HANDLE iocpHandle{};
		ulonglong_t rioKey{};

	private:
		std::mutex mutex;
		RIO_EXTENSION_FUNCTION_TABLE table{};
		RIO_CQ cq{ RIO_INVALID_CQ };
		RIO_NOTIFICATION_COMPLETION notifyCompletion{};
		OVERLAPPED notifyOverlapped{};
		bool_t isInitialized{ false };
		ulong_t cqSize{ 0 };
		std::unordered_map<socket_t, RIO_RQ> requestQueues;
		std::unordered_map<uint64_t, ne::byte_t*> regionBases;

	public: /* IRegisteredBufferProvider */
		/**
		 * @brief 사용자 메모리 영역을 RIORegisterBuffer 로 등록한다(필요 시 EnsureInitialized() 로 RIO 를 최초 초기화).
		 * @param _region 등록할 영역(비어 있으면 실패).
		 * @return 성공 시 등록 ID 를 담은 BufferHandle, 실패 시 IoError(WSA 에러 기반).
		 */
		[[nodiscard]] virtual ne::Result<BufferHandle, IoError> RegisterBuffer(std::span<ne::byte_t> _region) noexcept override;

		/**
		 * @brief 등록된 버퍼를 RIODeregisterBuffer 로 해제하고 내부 주소 캐시(regionBases)에서 제거한다.
		 * @param _handle RegisterBuffer() 가 반환했던 핸들. 무효 핸들은 조용히 무시된다.
		 */
		virtual void_t UnregisterBuffer(BufferHandle _handle) noexcept override;

	public: /* IRegisteredBufferProvider */
		/** @brief 등록된 버퍼로 zero-copy 송신을 제출한다(내부적으로 Submit(..., true) 로 위임). */
		[[nodiscard]] virtual ne::Result<void_t, IoError> SubmitSendRegistered(const socket_t _socket, const BufferHandle _handle, const void_t* _buffer, const std::size_t _length, void_t* _userData) noexcept override { return Submit(_socket, _handle, _buffer, _length, _userData, true); }
		/** @brief 등록된 버퍼로 zero-copy 수신을 제출한다(내부적으로 Submit(..., false) 로 위임). */
		[[nodiscard]] virtual ne::Result<void_t, IoError> SubmitReceiveRegistered(const socket_t _socket, const BufferHandle _handle, void_t* _buffer, const std::size_t _length, void_t* _userData) noexcept override { return Submit(_socket, _handle, _buffer, _length, _userData, false); }

	public: /* IRegisteredBufferProvider */
		/**
		 * @brief 소켓이 닫히기 전에 그 소켓에 대해 생성해 둔 RIO_RQ 항목을 requestQueues 에서 제거한다.
		 *
		 * @note RIO_RQ 자체를 명시적으로 닫는 API 는 없으며, 소켓이 닫히면 연관된 RQ 도 커널이 함께 정리한다.
		 * 여기서는 이 provider 내부의 소켓→RQ 매핑만 정리해 재사용 시 stale 핸들을 참조하지 않도록 한다.
		 *
		 * @param _socket 해제할 소켓.
		 */
		virtual void_t ReleaseSocket(socket_t _socket) noexcept override;

	private:
		/**
		 * @brief RIO 확장 함수 테이블을 최초 1회 로드하고 공유 RIO_CQ 를 생성한다(lazy init).
		 *
		 * 임시 소켓을 하나 만들어 WSAIoctl(SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTERS)로 RIO 함수
		 * 테이블 전체를 얻은 뒤 바로 닫고, RIOCreateCompletionQueue 로 IOCP 에 바인딩된 완료 큐를
		 * 만든다. 이미 초기화되어 있으면 아무 것도 하지 않고 성공을 반환한다.
		 *
		 * @return 초기화(또는 이미 완료됨) 성공 시 Ok, WSAIoctl/RIOCreateCompletionQueue 실패 시 IoError.
		 */
		[[nodiscard]] ne::Result<void_t, IoError> EnsureInitialized() noexcept;

		/**
		 * @brief 소켓별 RIO_RQ(Request Queue)를 처음 필요할 때 생성하고 이후에는 캐시된 것을 재사용한다.
		 * @param _socket 대상 소켓.
		 * @return 이미 있으면 기존 RQ, 없으면 RIOCreateRequestQueue 로 새로 만든 RQ. 실패 시 IoError.
		 * @note 호출 전 mutex 가 잠겨 있어야 한다("Locked" 접미사).
		 */
		[[nodiscard]] ne::Result<RIO_RQ, IoError> EnsureRequestQueueLocked(socket_t _socket) noexcept;

		/**
		 * @brief 사용자 버퍼 포인터를 등록된 영역 기준 상대 오프셋으로 변환해 RIO_BUF 를 구성한다.
		 *
		 * RIO API 는 절대 포인터가 아니라 (BufferId, Offset, Length) 로 버퍼를 지정하므로, _buffer 가
		 * regionBases 에 기록된 등록 영역 시작 주소로부터 얼마나 떨어져 있는지를 계산해 Offset 에 채운다.
		 *
		 * @param _handle 등록된 영역의 핸들(무효하거나 미등록이면 BufferId 가 0인 빈 RIO_BUF 반환).
		 * @param _buffer 등록 영역 내부의 실제 데이터 시작 주소.
		 * @param _length 사용할 길이.
		 * @return 채워진 RIO_BUF(실패 시 BufferId 가 0인 상태로 반환되며 호출자가 이를 검사해야 한다).
		 * @note 호출 전 mutex 가 잠겨 있어야 한다.
		 */
		[[nodiscard]] RIO_BUF MakeRioBufferLocked(BufferHandle _handle, const void_t* _buffer, std::size_t _length) const noexcept;

		/**
		 * @brief RIOSend/RIOReceive 공통 제출 경로. 버퍼 검증, lazy 초기화, RQ 확보를 순서대로 처리한다.
		 * @param _socket 대상 소켓.
		 * @param _handle 등록된 버퍼 핸들.
		 * @param _buffer 등록 영역 내부의 실제 주소.
		 * @param _length 송수신 길이.
		 * @param _userData 완료 시 RIORESULT::RequestContext 로 되돌아올 사용자 데이터.
		 * @param _isSend true 면 RIOSend, false 면 RIOReceive 를 호출한다.
		 * @return 제출 자체의 성공/실패(완료 결과는 이후 RIO_CQ 를 통해 비동기로 나온다).
		 */
		[[nodiscard]] ne::Result<void_t, IoError> Submit(socket_t _socket, BufferHandle _handle, const void_t* _buffer, std::size_t _length, void_t* _userData, bool_t _isSend) noexcept;

	public:
		/**
		 * @brief RIO_CQ 완료 통지를 재무장(re-arm)한다.
		 *
		 * RIONotify 는 one-shot 이라 한 번 통지되면 다시 호출해야 다음 완료가 또 IOCP 로 전달된다.
		 * IocpEngine::DrainRioCompletions() 가 완료를 회수한 직후 항상 이 함수를 호출해야 완료 누락이
		 * 없다.
		 *
		 * @return 성공 시 Ok, RIO_CQ 가 아직 초기화되지 않았거나 RIONotify 실패 시 IoError.
		 */
		[[nodiscard]] ne::Result<void_t, IoError> ArmNotify() noexcept;

	public:
		/** @brief RIO 확장 함수 테이블/완료 큐가 이미 초기화되었는지 반환한다. */
		[[nodiscard]] bool_t IsInitialized() const noexcept { return isInitialized; }
		/** @brief 로드된 RIO 확장 함수 테이블에 대한 참조를 반환한다(RIODequeueCompletion 등 직접 호출용). */
		[[nodiscard]] const RIO_EXTENSION_FUNCTION_TABLE& Table() const noexcept { return table; }
		/** @brief 이 provider 가 소유한 공유 RIO 완료 큐 핸들을 반환한다. */
		[[nodiscard]] RIO_CQ CompletionQueue() const noexcept { return cq; }
	};

END_NS

#endif // _WIN32
