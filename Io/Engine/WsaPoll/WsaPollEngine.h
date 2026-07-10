//
// Created by hscloud on 26. 7. 8.
//
// Level 0 — Windows WSAPoll 백엔드(Reactor). EpollEngine 과 대칭되는 구조 — readiness 알림을
// 받아 recv/send/accept/connect 를 직접 수행하고 synthetic completion 을 만들어 완료 기반
// IEngine 계약(Submit/WaitCompletions)으로 노출한다. IocpEngine 생성이 실패하는 극히
// 예외적인 상황에서만 EngineFactory 가 내부적으로 선택하는 폴백 — 공개 API 로 노출하지 않는다.
//   Submit          : op 를 즉시 non-blocking 시도. 완료면 합성 완료 큐로, WSAEWOULDBLOCK 이면 등록.
//   WaitCompletions : 합성 완료 배출 → WSAPoll(매 호출 fd 목록 재구성) → 준비된 fd 의 대기 op 수행.
//   Wake / Cancel   : 로컬 루프백 소켓 쌍(Windows 에 eventfd/socketpair 없음) / 지연취소(-ERROR_OPERATION_ABORTED 합성 완료).
//
// 파일 Read/Write 는 epoll 이 일반 파일에 readiness 를 줄 수 없는 것과 대칭이다 — WSAPoll 로
// 기다리지 않고, 파일 핸들이 항상 FILE_FLAG_OVERLAPPED 로 열리는 점을 이용해 ReadFile/WriteFile
// + 로컬 OVERLAPPED(offset) + GetOverlappedResult(wait=TRUE) 로 Perform() 안에서 즉시 동기 완료시킨다.

#pragma once
#include "Base/Type.h"

#if defined(_WIN32)
#	include <mutex>
#	include <vector>
#	include <unordered_map>
#	include "Io/Engine/IEngine.h"

BEGIN_NS(ne::io)
	class WsaPollEngine final :public IEngine
	{
	public:
		WsaPollEngine() noexcept;
		virtual ~WsaPollEngine() override;

		NEBULA_NON_COPYABLE_MOVABLE(WsaPollEngine)

	private:
		// readiness 대기 중인 op — 준비되면 그대로 재수행한다.
		struct PendingOperation
		{
			Request request;
			bool_t isWrite; // true=POLLOUT(write/send/connect), false=POLLIN(read/receive/accept)
		};

	private:
		ulonglong_t wakeReadSocket{ 0 }; // socket_t 를 64비트로 정규화(Request.handle 과 동일 관례)
		ulonglong_t wakeWriteSocket{ 0 };
		bool_t isValid{ false };
		std::mutex mutex;
		std::unordered_map<void_t*, PendingOperation> pending; // userData → 대기 op
		std::unordered_map<ulonglong_t, void_t*> readWaiter;   // fd → POLLIN 대기 userData
		std::unordered_map<ulonglong_t, void_t*> writeWaiter;  // fd → POLLOUT 대기 userData
		std::vector<Completion> ready;                         // 즉시/합성 완료
		std::vector<void_t*> pendingCancels;

	public:
		virtual void_t Submit(const Request& _request) override;
		[[nodiscard]] virtual int_t WaitCompletions(Completion* _out, int_t _max, std::chrono::milliseconds _timeout) override;
		virtual void_t Wake() override;
		virtual void_t Cancel(void_t* _userData) noexcept override;
		[[nodiscard]] virtual bool_t Supports(Capability _capability) const noexcept override;
		[[nodiscard]] virtual bool_t IsValid() const noexcept override { return isValid; }

	private:
		// op 를 즉시 수행(파일은 항상, 소켓은 non-blocking) 시도한다.
		// true=완료(_result 설정), false=WSAEWOULDBLOCK(WSAPoll 대기 필요).
		[[nodiscard]] bool_t Perform(const Request& _request, bool_t _isRetry, longlong_t& _result) noexcept;
		[[nodiscard]] static bool_t IsWriteDirection(OpCode _op) noexcept;
		void_t ProcessCancels();
	};

END_NS

#endif // _WIN32
