//
// Created by hscloud on 26. 7. 8.
//

#pragma once
#include "Base/Type.h"

#if defined(_WIN32)
#	include <mutex>
#	include <vector>
#	include <unordered_map>
#	include "Io/Engine/IEngine.h"

BEGIN_NS(ne::io)
	/**
	 * @class WsaPollEngine
	 * @brief Windows WSAPoll 기반 reactor 엔진.
	 *
	 * EpollEngine 과 대칭되는 구조로, readiness 알림(WSAPoll)을 받아 recv/send/accept/connect 를
	 * 직접 수행하고 synthetic completion 을 만들어 IEngine 의 완료 기반 계약으로 노출한다.
	 * IocpEngine 생성이 실패하는 예외적인 상황에서만 EngineFactory 가 내부적으로 선택하는
	 * 폴백이며, 공개 API 로 별도 노출되지 않는다. 파일 Read/Write 는 WSAPoll 로 기다리지 않고
	 * OVERLAPPED + GetOverlappedResult(wait=TRUE) 로 Perform() 안에서 즉시 동기 완료시킨다.
	 * Wake 는 로컬 루프백 TCP 소켓 쌍으로 구현한다(Windows 에는 eventfd/socketpair 가 없음).
	 *
	 * @note 등록 버퍼(zero-copy, RIO) 를 지원하지 않는다.
	 */
	class WsaPollEngine final :public IEngine
	{
	public:
		/**
		 * @brief Wake() 용 로컬 루프백 TCP 소켓 쌍을 만들어 non-blocking 으로 전환한다.
		 * @note Windows 에는 POSIX eventfd/socketpair 가 없어, 127.0.0.1 로 자기 자신에게 connect
		 * 하는 TCP 소켓 쌍으로 그 역할을 대신한다. 생성 실패 시 isValid 가 false 로 남는다.
		 */
		WsaPollEngine() noexcept;
		/** @brief Wake() 용 소켓 쌍을 닫는다. */
		virtual ~WsaPollEngine() override;

		NEBULA_NON_COPYABLE_MOVABLE(WsaPollEngine)

	private:
		struct PendingOperation
		{
			Request request;
			bool_t isWrite;
		};

	private:
		ulonglong_t wakeReadSocket{ 0 };
		ulonglong_t wakeWriteSocket{ 0 };
		bool_t isValid{ false };
		std::mutex mutex;
		std::unordered_map<void_t*, PendingOperation> pending;
		std::unordered_map<ulonglong_t, void_t*> readWaiter;
		std::unordered_map<ulonglong_t, void_t*> writeWaiter;
		std::vector<Completion> ready;
		std::vector<void_t*> pendingCancels;

	public: /* IEngine */
		/**
		 * @brief 요청을 즉시 시도하고, 소켓 recv/send/accept/connect 계열만 WSAPoll 대기로 넘긴다.
		 *
		 * 파일 Read/Write 는 WSAPoll 로 기다릴 대상이 아니므로(핸들 타입 자체가 poll 불가) Perform()
		 * 안에서 OVERLAPPED + GetOverlappedResult(TRUE) 로 항상 동기적으로 완료된다. 소켓 op 는
		 * EpollEngine 과 동일하게 우선 non-blocking 시도 후 WSAEWOULDBLOCK 이면 pending 에 등록한다.
		 */
		virtual void_t Submit(const Request& _request) override;
		/**
		 * @brief ready 큐를 먼저 비우고, 없으면 WSAPoll 로 대기해 준비된 소켓의 요청을 수행한다.
		 *
		 * Wake() 용 소켓(wakeReadSocket)도 다른 항목과 함께 WSAPoll 대상에 포함시켜 감시하며,
		 * 신호가 오면 그 데이터를 모두 비우고 실제 완료로는 노출하지 않는다. 그 외 소켓은
		 * read/write 두 방향을 모두 검사해 pending 요청을 Perform(..., true, ...) 으로 재수행한다.
		 *
		 * @return 채워진 완료 개수.
		 */
		[[nodiscard]] virtual int_t WaitCompletions(Completion* _out, int_t _max, std::chrono::milliseconds _timeout) override;
		/** @brief wakeWriteSocket 으로 1바이트를 send 해 WSAPoll 로 블로킹 중인 스레드를 깨운다. */
		virtual void_t Wake() override;
		/**
		 * @brief 취소 요청을 pendingCancels 에 적재하고 Wake() 로 대기 중인 루프를 깨운다.
		 * 실제 취소 처리는 다음 WaitCompletions() 시작 시점의 ProcessCancels() 에서 수행된다.
		 */
		virtual void_t Cancel(void_t* _userData) noexcept override;
		/** @brief SEND_FILE_ZERO_COPY(TransmitFile)만 지원, 나머지 zero-copy 계열은 미지원임을 알린다. */
		[[nodiscard]] virtual bool_t Supports(Capability _capability) const noexcept override;
		/** @brief 생성자에서 Wake() 용 소켓 쌍 확보에 성공했는지 반환한다. */
		[[nodiscard]] virtual bool_t IsValid() const noexcept override { return isValid; }

	private:
		/**
		 * @brief 요청 하나를 해당 RequestKind 에 맞는 Win32/Winsock API 로 실제 수행한다.
		 *
		 * READ/WRITE(파일)는 chain 여부와 무관하게 OVERLAPPED 를 만들어 즉시 호출하고,
		 * ERROR_IO_PENDING 이면 GetOverlappedResult(..., TRUE) 로 완료까지 블로킹 대기해 동기
		 * 완료로 확정한다(파일 핸들은 WSAPoll 감시 대상이 될 수 없기 때문). 소켓 계열
		 * (RECEIVE/SEND/ACCEPT/CONNECT 등)은 recv/send/accept/connect 를 non-blocking 으로 호출해
		 * WSAEWOULDBLOCK 이면 false 를 반환하고, 그 외 결과는 true 로 확정한다. CONNECT 는
		 * EpollEngine 과 동일하게 최초 시도 시 진행 중이면 false, 재시도(_isRetry) 시 SO_ERROR 로
		 * 실제 성공 여부를 판정한다.
		 *
		 * @param _request 수행할 요청.
		 * @param _isRetry WSAPoll 로 대기했다가 재시도하는 경우 true.
		 * @param _result 완료로 확정된 경우 채워질 바이트 수(성공) 또는 -winsock/win32 에러 코드(실패).
		 * @return 완료가 확정되었으면 true, 아직 준비되지 않아 재시도가 필요하면 false.
		 */
		[[nodiscard]] bool_t Perform(const Request& _request, bool_t _isRetry, longlong_t& _result) noexcept;

		/** @brief 해당 RequestKind 가 쓰기(POLLWRNORM) 방향 대기가 필요한 연산인지 판별한다. */
		[[nodiscard]] static bool_t IsWriteDirection(RequestKind _requestKind) noexcept;

		/**
		 * @brief pendingCancels 에 쌓인 취소 요청을 실제로 처리해 ready 큐에 합성 완료를 만든다.
		 * EpollEngine::ProcessCancels() 와 동일한 구조이며, ERROR_OPERATION_ABORTED 를 결과로 사용한다.
		 */
		void_t ProcessCancels();
	};

END_NS

#endif // _WIN32
