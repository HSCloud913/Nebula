//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include "Base/Type.h"

#if defined(IS_POSIX)
#	include <liburing.h>
#	include <memory>
#	include <mutex>
#	include <vector>
#	include <unordered_map>
#	include <sys/uio.h>
#	include <sys/socket.h>
#	include "Io/Engine/IEngine.h"
#	include "Io/Engine/IoUring/Provider/IoUringProvider.h"

BEGIN_NS(ne::io)
	/**
	 * @class IoUringEngine
	 * @brief Linux io_uring(커널 5.1+) 기반 proactor 엔진.
	 *
	 * op 별로 SQE(Submission Queue Entry)를 준비해 제출하고, CQE(Completion Queue Entry)를
	 * peek_batch_cqe/wait_cqe_timeout 으로 회수해 IEngine::Completion 으로 정규화한다. ring 은
	 * 단일 루프 스레드에서만 조작하며, 다른 스레드의 Cancel()/Wake() 요청은 pendingCancels 로
	 * 지연시켰다가 루프 스레드가 ASYNC_CANCEL SQE 로 제출한다. IoUringProvider 를 합성해
	 * ReadFixed/WriteFixed 용 등록 버퍼(Fixed Buffer)를 제공한다.
	 */
	class IoUringEngine final :public IEngine
	{
	public:
		NEBULA_NON_COPYABLE_MOVABLE(IoUringEngine)

		/**
		 * @brief io_uring_queue_init 으로 SQ/CQ 링을 만들고, Wake() 용 eventfd 를 준비해 poll 로 등록한다.
		 * @param _queueDepth SQ(Submission Queue) 깊이. 값이 클수록 한 번에 더 많은 미완료 요청을 버틸 수 있다.
		 * @note 링 생성 또는 eventfd 생성에 실패하면 isValid 가 false 로 남는다.
		 */
		explicit IoUringEngine(uint_t _queueDepth = 256) noexcept;
		/** @brief eventfd 를 닫고, 링이 유효했다면 io_uring_queue_exit 으로 커널 링 자원을 해제한다. */
		virtual ~IoUringEngine() override;

	private:
		static constexpr int_t MaxBatch = 128;
		static constexpr ulonglong_t WakeUserData = ~0ULL;

		struct UringOperation
		{
			void_t* userData;
			RequestKind requestKind;
			bool_t isUnsupported;
			std::vector<iovec> iovecs;
			msghdr message{};
			int_t* fromAddressLength{ nullptr };
		};

	private:
		io_uring ring{};
		bool_t isValid{ false };
		int_t wakeEventFd{ -1 };
		std::mutex mutex;
		std::unordered_map<void_t*, UringOperation*> inflight;
		std::vector<void_t*> pendingCancels;
		std::vector<Completion> readyCompletions;
		std::unique_ptr<IoUringProvider> bufferProvider;

	public: /* IEngine */
		/**
		 * @brief 요청에 맞는 io_uring_prep_* 로 SQE 를 채워 커널에 제출한다.
		 *
		 * SEND_FILE 만 예외적으로 io_uring 에 전용 prep 함수가 없어 sendfile() 을 동기 호출하고
		 * 결과를 readyCompletions 에 바로 적재한다. 그 외에는 UringOperation 을 heap 에 만들어
		 * SQE 의 user_data 로 등록(io_uring_sqe_set_data)하고, chain 이 있으면 iovec/msghdr 로
		 * 변환해 벡터 I/O(readv/writev/recvmsg/sendmsg) 로 제출한다. SQE 확보에 실패하면(SQ 가
		 * 가득 찬 경우) ENOBUFS 로 즉시 실패 완료를 만든다.
		 */
		virtual void_t Submit(const Request& _request) override;

		/**
		 * @brief 준비된 완료(readyCompletions)를 먼저 비우고, 없으면 CQE 를 배치로 회수한다.
		 *
		 * io_uring_wait_cqe(_timeout) 로 최소 한 개의 CQE 를 기다린 뒤, io_uring_peek_batch_cqe 로
		 * 그 시점에 이미 준비된 나머지 CQE 들까지 한 번에 긁어온다(반복 wait 를 피해 배치 처리
		 * 효율을 높임). WakeUserData 로 마킹된 CQE(Wake()/취소용 poll)는 실제 완료로 노출하지
		 * 않고 소비만 하며, 발견되면 wakeEventFd 를 비우고 ArmWakePoll() 로 다음 Wake() 를 위한
		 * poll 을 재등록한다.
		 *
		 * @return 채워진 완료 개수. 타임아웃이면 0.
		 */
		[[nodiscard]] virtual int_t WaitCompletions(Completion* _out, int_t _max, std::chrono::milliseconds _timeout) override;

		/** @brief wakeEventFd 에 1을 write 해 io_uring_wait_cqe 로 블로킹 중인 스레드를 깨운다. */
		virtual void_t Wake() override;
		/**
		 * @brief 취소 요청을 pendingCancels 에 적재하고 Wake() 로 루프 스레드를 깨운다.
		 *
		 * io_uring SQ/CQ 링은 단일 스레드에서만 조작해야 안전하므로, 다른 스레드에서 호출되었을
		 * 이 함수는 즉시 io_uring_prep_cancel 을 호출하지 않고 예약만 한다. 실제 ASYNC_CANCEL SQE
		 * 제출은 루프 스레드의 WaitCompletions() 안에서 SubmitPendingCancels() 가 수행한다.
		 *
		 * @param _userData Submit() 에 전달했던 식별자.
		 */
		virtual void_t Cancel(void_t* _userData) noexcept override;

		/** @brief SEND_FILE_ZERO_COPY/SEND_MEM_ZERO_COPY/RECEIVE_OVERHEAD_REDUCED(고정 버퍼) 지원을 알린다. */
		[[nodiscard]] virtual bool_t Supports(Capability _capability) const noexcept override;
		/** @brief 링 초기화와 Wake() 용 eventfd 확보가 모두 성공했는지 반환한다. */
		[[nodiscard]] virtual bool_t IsValid() const noexcept override { return isValid; }
		/** @brief io_uring Fixed Buffer 기반 등록 버퍼 provider(IoUringProvider)를 노출한다. */
		[[nodiscard]] virtual IRegisteredBufferProvider* AsRegisteredBufferProvider() noexcept override { return bufferProvider.get(); }

	private:
		/**
		 * @brief 링에서 빈 SQE 하나를 가져오며, 가득 찬 경우 즉시 제출(io_uring_submit)해 공간을 비우고 재시도한다.
		 * @return 확보된 SQE 포인터. 그래도 실패하면 nullptr(호출자가 실패 완료로 처리해야 함).
		 */
		[[nodiscard]] io_uring_sqe* AcquireSqe() noexcept;

		/**
		 * @brief wakeEventFd 를 POLLIN 으로 감시하는 SQE 를 다시 등록한다(poll 은 1회성이라 재등록 필요).
		 *
		 * user_data 는 WakeUserData(~0ULL) 로 고정해, 실제 UringOperation 포인터와 절대 충돌하지
		 * 않는 값으로 Wake 신호 CQE 를 구분할 수 있게 한다.
		 */
		void_t ArmWakePoll() noexcept;

		/**
		 * @brief Cancel() 이 예약해 둔 취소 요청들을 실제로 io_uring_prep_cancel SQE 로 제출한다.
		 *
		 * pendingCancels 를 잠금 하에 스왑한 뒤, 각 userData 에 대응하는 UringOperation 이 여전히
		 * inflight 에 있으면 그 포인터를 취소 대상으로 지정해 ASYNC_CANCEL 을 제출한다. 이미 완료돼
		 * inflight 에서 사라진 요청은 조용히 건너뛴다. 취소 SQE 의 user_data 도 WakeUserData 로
		 * 마킹해 일반 완료 처리 경로와 섞이지 않게 한다.
		 */
		void_t SubmitPendingCancels() noexcept;
	};

END_NS

#endif // IS_POSIX
