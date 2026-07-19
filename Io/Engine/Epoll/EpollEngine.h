//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include "Base/Type.h"

#if defined(IS_POSIX)
#	include "Io/Engine/IEngine.h"
#	include <mutex>
#	include <vector>
#	include <unordered_map>

BEGIN_NS(ne::io)
	/**
	 * @class EpollEngine
	 * @brief Linux epoll 기반 reactor 엔진.
	 *
	 * readiness 알림(epoll_wait)을 받아 read/write 계열 시스템 콜을 직접 수행하고, 그 결과를
	 * synthetic completion 으로 만들어 IEngine 의 완료 기반(proactor) 계약으로 노출한다. 요청은
	 * 우선 non-blocking 으로 즉시 시도하고, EAGAIN 이면 epoll 에 등록해 두었다가 준비되면
	 * WaitCompletions() 안에서 재수행한다. Wake 는 eventfd, Cancel 은 지연 처리 후 합성 완료로
	 * 통지한다.
	 *
	 * @note 등록 버퍼(zero-copy) provider 를 지원하지 않는다.
	 */
	class EpollEngine final :public IEngine
	{
	public:
		/**
		 * @brief epoll 인스턴스와 Wake() 용 eventfd 를 생성하고 eventfd 를 epoll 에 등록한다.
		 *
		 * epoll_create1/eventfd/epoll_ctl 중 하나라도 실패하면 이미 만든 fd 를 정리하고
		 * isValid 를 false 로 남긴다(예외를 던지지 않는다).
		 */
		EpollEngine() noexcept;
		/** @brief eventfd 와 epoll fd 를 닫는다. */
		virtual ~EpollEngine() override;

		NEBULA_NON_COPYABLE_MOVABLE(EpollEngine)

	private:
		static constexpr int_t MaxEvents = 64;

		struct PendingOperation
		{
			Request request;
			bool_t isWrite;
		};

	private:
		int_t epollFd{ -1 };
		int_t wakeEventFd{ -1 };
		bool_t isValid{ false };
		std::mutex mutex;
		std::unordered_map<void_t*, PendingOperation> pending;
		std::unordered_map<int_t, void_t*> readWaiter;
		std::unordered_map<int_t, void_t*> writeWaiter;
		std::vector<Completion> ready;
		std::vector<void_t*> pendingCancels;

	public: /* IEngine */
		/**
		 * @brief 요청을 즉시 non-blocking 으로 시도하고, 안 되면 epoll 에 등록해 대기시킨다.
		 *
		 * 먼저 Perform(_request, false, ...) 으로 즉시 완료를 시도한다. 성공(또는 즉시 확정된
		 * 에러)하면 결과를 ready 큐에 넣어 다음 WaitCompletions() 에서 바로 소비되게 한다.
		 * EAGAIN 등으로 당장 처리할 수 없으면 pending 맵에 저장하고 read/write 방향에 따라
		 * readWaiter/writeWaiter 에 등록한 뒤 UpdateEpoll() 로 해당 fd 의 관심 이벤트를 갱신한다.
		 */
		virtual void_t Submit(const Request& _request) override;

		/**
		 * @brief ready 큐를 먼저 비우고, 없으면 epoll_wait 로 대기해 준비된 fd 들의 요청을 수행한다.
		 *
		 * 순서: 1) ProcessCancels() 로 대기 중인 취소를 합성 완료로 변환, 2) ready 큐에 이미
		 * 쌓인 완료가 있으면 그것부터 최대 _max 개 즉시 반환, 3) 없으면 epoll_wait(MaxEvents 개
		 * 배치)로 대기한다. wakeEventFd 이벤트는 Wake() 신호이므로 버퍼만 비우고 건너뛴다.
		 * 그 외 fd 는 read/write 두 방향을 모두 검사해 해당 pending 요청을 Perform(..., true, ...)
		 * 으로 재수행하고, 성공하면 waiter/pending 에서 제거한 뒤 UpdateEpoll() 로 관심 이벤트를
		 * 갱신한다.
		 *
		 * @return 채워진 완료 개수(0이면 타임아웃 또는 이번 반복에서 처리할 완료 없음).
		 */
		[[nodiscard]] virtual int_t WaitCompletions(Completion* _out, int_t _max, std::chrono::milliseconds _timeout) override;

		/** @brief wakeEventFd 에 1을 write 해 epoll_wait 로 블로킹 중인 스레드를 깨운다. */
		virtual void_t Wake() override;

		/**
		 * @brief 취소 요청을 pendingCancels 에 적재하고 Wake() 로 루프를 깨운다.
		 *
		 * 실제 취소 처리(waiter/pending 제거, 합성 완료 생성)는 다음 WaitCompletions() 호출
		 * 시작 부분의 ProcessCancels() 에서 수행된다. 즉시 처리하지 않는 이유는 Submit/
		 * WaitCompletions 를 호출하는 스레드가 다를 수 있어 mutex 경합 없이 예약만 해두기 위함이다.
		 */
		virtual void_t Cancel(void_t* _userData) noexcept override;

		/** @brief SEND_FILE_ZERO_COPY/SEND_MEM_ZERO_COPY 만 지원(RECEIVE 쪽 zero-copy 는 미지원)함을 알린다. */
		[[nodiscard]] virtual bool_t Supports(Capability _capability) const noexcept override;

		/** @brief 생성자에서 epoll fd/eventfd 확보에 모두 성공했는지 반환한다. */
		[[nodiscard]] virtual bool_t IsValid() const noexcept override { return isValid; }

	private:
		/**
		 * @brief 요청 하나를 해당 RequestKind 에 맞는 POSIX 시스템 콜로 실제 수행한다.
		 *
		 * pread/pwrite/preadv/pwritev/recv/send/recvmsg/sendmsg/sendto/recvfrom/accept/connect/
		 * sendfile 등을 RequestKind 에 따라 직접 호출한다. _request.chain 이 있으면 iovec 배열로
		 * 변환해 벡터 I/O(preadv/pwritev/recvmsg/sendmsg)를 사용한다. 반환값이 음수이고
		 * errno 가 EAGAIN/EWOULDBLOCK 이면 false 를 반환해 호출자가 epoll 대기로 넘어가게 하고,
		 * 그 밖의 실패는 -errno 를 _result 에 채워 true(완료 확정)로 반환한다. CONNECT 는
		 * 최초 호출 시 EINPROGRESS 면 false, 이후 _isRetry 로 재호출되면 SO_ERROR 로 실제 성공
		 * 여부를 확인한다. WAIT_READABLE/WAIT_WRITABLE 은 _isRetry 가 아니면 항상 false(즉,
		 * 반드시 한 번은 epoll 을 거치게 강제)를 반환한다.
		 *
		 * @param _request 수행할 요청.
		 * @param _isRetry epoll 로 대기했다가 재시도하는 경우 true(최초 시도는 false).
		 * @param _result 완료로 확정된 경우 채워질 바이트 수(성공) 또는 -errno(실패).
		 * @return 완료가 확정되었으면 true(성공/실패 모두), 아직 준비 안 되어 재시도가 필요하면 false.
		 */
		[[nodiscard]] bool_t Perform(const Request& _request, bool_t _isRetry, longlong_t& _result) noexcept;

		/** @brief 해당 RequestKind 가 쓰기(EPOLLOUT) 방향 대기가 필요한 연산인지 판별한다(WRITE/SEND/CONNECT 등). */
		[[nodiscard]] static bool_t IsWriteDirection(RequestKind _requestKind) noexcept;

		/**
		 * @brief readWaiter/writeWaiter 맵 상태를 기준으로 fd 의 epoll 관심 이벤트를 재계산해 반영한다.
		 *
		 * 어느 쪽도 대기 중이 아니면 EPOLL_CTL_DEL 로 등록을 제거하고, 그렇지 않으면
		 * EPOLLIN/EPOLLOUT 조합으로 EPOLL_CTL_MOD 를 시도하되, 아직 등록되지 않은 fd 라면
		 * MOD 가 실패하므로 EPOLL_CTL_ADD 로 폴백한다.
		 *
		 * @param _fd 갱신할 파일 디스크립터.
		 */
		void_t UpdateEpoll(int_t _fd) noexcept;

		/**
		 * @brief pendingCancels 에 쌓인 취소 요청을 실제로 처리해 ready 큐에 합성 완료를 만든다.
		 *
		 * mutex 를 짧게 잡고 pendingCancels 전체를 로컬 벡터로 스왑한 뒤, 각 userData 에 대해
		 * pending/waiter 맵에서 제거하고 UpdateEpoll() 로 관심 이벤트를 갱신하며, ECANCELED 를
		 * 결과로 하는 Completion 을 ready 큐에 추가한다. 이미 완료되어 pending 에 없는 userData 는
		 * 무시한다.
		 */
		void_t ProcessCancels();
	};

END_NS

#endif // IS_POSIX
