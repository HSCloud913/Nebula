//
// Created by hscloud on 26. 6. 30.
//
// Level 0 — Linux io_uring 백엔드. native 완료 큐를 IIoEngine 계약에 그대로 매핑한다(스펙 2.1).
//   Submit          : op 별 SQE 준비(prep_read/write/recv/send/accept/connect) + user_data=op
//   WaitCompletions : wait_cqe_timeout → peek_batch_cqe → IoCompletion 으로 정규화(cqe->res)
//   Wake            : eventfd 에 write → 등록해둔 POLL_ADD 완료로 대기 해제
//   Cancel          : 루프 스레드로 지연(pendingCancels + Wake) → ASYNC_CANCEL SQE (ring 단일 스레드 유지)
//   Supports        : capability 매트릭스(스펙 2.2)

#pragma once
#include "Type.h" // IS_POSIX 정의 — 아래 가드 전에 반드시 포함(빌트인 아님)
#if defined(IS_POSIX)

#include "Engine/IIoEngine.h"
#include <liburing.h>
#include <mutex>
#include <vector>
#include <unordered_map>

BEGIN_NS(ne::io)
	class IoUringEngine final :public IIoEngine
	{
	public:
		NEBULA_NON_COPYABLE_MOVABLE(IoUringEngine)

		explicit IoUringEngine(uint_t _queueDepth = 256) noexcept;
		virtual ~IoUringEngine() override;

	private:
		static constexpr int_t MaxBatch = 128;
		static constexpr ulonglong_t WakeUserData = ~0ULL; // eventfd POLL_ADD 완료 식별 sentinel

		// 제출 op 당 컨텍스트 — SQE user_data 가 이걸 가리킨다. 완료 시 userData 를 되돌린다.
		struct UringOperation
		{
			void*  userData;
			OpCode op;
			bool_t unsupported; // 미지원 op(Level 3.5 등) 은 nop 로 제출하고 완료에서 -에러로 변환
		};

		io_uring   ring{};
		bool_t     valid{ false };
		int_t      wakeEventFd{ -1 };
		std::mutex mutex; // inflight / pendingCancels / readyCompletions 보호(Cancel·Wake 는 타 스레드 가능)
		std::unordered_map<void*, UringOperation*> inflight; // handler(userData) → op (Cancel 조회)
		std::vector<void*>        pendingCancels;   // 타 스레드 Cancel 요청 — 루프 스레드가 ASYNC_CANCEL 제출
		std::vector<IoCompletion> readyCompletions; // SQ 부족 등 즉시 완료(합성)

	public:
		void_t Submit(const IoRequest& _request) override;
		[[nodiscard]] int_t WaitCompletions(IoCompletion* _out, int_t _max, std::chrono::milliseconds _timeout) override;
		void_t Wake() override;
		void_t Cancel(void* _userData) noexcept override;
		[[nodiscard]] bool_t Supports(Capability _capability) const noexcept override;

	private:
		[[nodiscard]] io_uring_sqe* AcquireSqe() noexcept;           // 없으면 submit 후 재시도
		void_t ArmWakePoll() noexcept;                               // eventfd POLL_ADD 재무장
		void_t SubmitPendingCancels() noexcept;                      // 루프 스레드에서 지연 취소 제출

	public:
		[[nodiscard]] bool_t IsValid() const noexcept { return valid; }
	};

END_NS

#endif // IS_POSIX
