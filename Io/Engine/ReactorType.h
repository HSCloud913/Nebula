//
// Created by csw on 26. 7. 7..
//

#pragma once
#include <functional>
#include "Type.h"
#include "IoType.h"

BEGIN_NS(ne::io)
	using IoCallback = std::function<void(socket_t _fd, uint_t _events)>;

	struct IoEvent
	{
		static constexpr uint_t Read = 1u << 0;
		static constexpr uint_t Write = 1u << 1;
		static constexpr uint_t Error = 1u << 2;
		static constexpr uint_t HangUp = 1u << 3;
	};

	struct WatchEntry
	{
#if defined(_WIN32)
		// IOCP 는 zero-byte WSARecv/WSASend 가 완료(GQCS 로 회수)되기 전까지 overlapped 를
		// 커널이 계속 참조한다. 그 전에 재사용(재무장)하거나 엔트리를 해제하면 각각 메모리
		// 오염 / use-after-free 가 되므로, pending 중 재등록·해제 요청은 즉시 처리하지 않고
		// pendingAction 에 적어 두었다가 실제 완료가 RunOnce 로 돌아온 뒤 처리한다.
		enum class PendingAction : ushort_t
		{
			NONE,
			REARM,
			RELEASE
		};

		OVERLAPPED overlapped{};
		socket_t fd{};
		bool_t isPending{ false };
		PendingAction pendingAction{ PendingAction::NONE };
#elif defined(IS_POSIX)
		uint_t generation{};
#endif
		uint_t events{};
		IoCallback callback;
	};

	// fd 하나에 대한 Read 방향과 Write 방향 감시를 서로 독립된 WatchEntry 로 보관한다.
	// 예: 한 코루틴이 recv 를 기다리는 동안 다른 코루틴이 같은 fd 로 send 를 기다릴 수 있는데,
	// 두 감시가 하나의 엔트리를 공유하면 나중 Watch 호출이 먼저 것의 콜백/제출 상태를 덮어써
	// 버린다. Slot() 은 이벤트 마스크의 Write 비트로 방향을 정한다(Read/Write 를 한 Watch 호출에
	// 같이 요청하는 caller 는 없음 — ReceiveAwaitable/SendAwaitable 등은 항상 한쪽만 요청한다).
	struct WatchSlots
	{
		WatchEntry read;
		WatchEntry write;

		[[nodiscard]] WatchEntry& Slot(const uint_t _events) noexcept { return (_events & IoEvent::Write) ? write : read; }
		[[nodiscard]] bool_t Empty() const noexcept { return !read.callback && !write.callback; }
	};

END_NS
