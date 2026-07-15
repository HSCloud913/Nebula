//
// Created by hscloud on 25. 6. 29.
//
// Level 0 — Linux epoll 백엔드(Reactor). readiness 알림을 받아 read/write 를 직접 수행하고
// synthetic completion 을 만들어 완료 기반 IEngine 계약(Submit/WaitCompletions)으로 노출한다.
//   Submit          : op 를 즉시 non-blocking 시도. 완료면 합성 완료 큐로, EAGAIN 이면 epoll 등록.
//   WaitCompletions : 합성 완료 배출 → epoll_wait → 준비된 fd 의 대기 op 를 수행해 완료 생성.
//   Wake / Cancel   : eventfd / 지연취소(-ECANCELED 합성 완료).

#pragma once
#include "Base/Type.h" // IS_POSIX 정의 — 아래 가드 전에 반드시 포함(빌트인 아님)

#if defined(IS_POSIX)
#	include "Io/Engine/IEngine.h"
#	include <mutex>
#	include <vector>
#	include <unordered_map>

BEGIN_NS(ne::io)
	class EpollEngine final :public IEngine
	{
	public:
		EpollEngine() noexcept;
		virtual ~EpollEngine() override;

		NEBULA_NON_COPYABLE_MOVABLE(EpollEngine)

	private:
		static constexpr int_t MaxEvents = 64;

		// readiness 대기 중인 op — 준비되면 그대로 재수행한다.
		struct PendingOperation
		{
			Request request;
			bool_t isWrite; // true=EPOLLOUT(write/send/connect), false=EPOLLIN(read/receive/accept)
		};

	private:
		int_t epollFd{ -1 };
		int_t wakeEventFd{ -1 };
		bool_t isValid{ false };
		std::mutex mutex;
		std::unordered_map<void_t*, PendingOperation> pending; // userData → 대기 op
		std::unordered_map<int_t, void_t*> readWaiter;         // fd → EPOLLIN 대기 userData
		std::unordered_map<int_t, void_t*> writeWaiter;        // fd → EPOLLOUT 대기 userData
		std::vector<Completion> ready;                         // 즉시/합성 완료
		std::vector<void_t*> pendingCancels;

	public: /* IEngine */
		virtual void_t Submit(const Request& _request) override;
		[[nodiscard]] virtual int_t WaitCompletions(Completion* _out, int_t _max, std::chrono::milliseconds _timeout) override;
		virtual void_t Wake() override;
		virtual void_t Cancel(void_t* _userData) noexcept override;
		[[nodiscard]] virtual bool_t Supports(Capability _capability) const noexcept override;
		[[nodiscard]] virtual bool_t IsValid() const noexcept override { return isValid; }

	private:
		// op 를 즉시 non-blocking 수행. true=완료(_result 설정), false=EAGAIN(epoll 대기 필요).
		[[nodiscard]] bool_t Perform(const Request& _request, bool_t _isRetry, longlong_t& _result) noexcept;
		[[nodiscard]] static bool_t IsWriteDirection(OpCode _op) noexcept;
		void_t UpdateEpoll(int_t _fd) noexcept; // readWaiter/writeWaiter 기준 epoll_ctl ADD/MOD/DEL
		void_t ProcessCancels();
	};

END_NS

#endif // IS_POSIX
