//
// Created by hscloud on 26. 7. 1.
//

#pragma once
#include <coroutine>
#include <cstdint>
#include <functional>
#include "Result.h"
#include "Error.h"
#include "Type.h"
#include "IoType.h"

#if defined(_WIN32)
#   include <winsock2.h>
#endif

namespace ne::time
{
	class TimerWheel;
}

BEGIN_NS(ne::io)
	using IoCallback = std::function<void(socket_t _fd, uint32_t _events)>;

	struct IoEvent
	{
		static constexpr uint32_t Read = 1u << 0;
		static constexpr uint32_t Write = 1u << 1;
		static constexpr uint32_t Error = 1u << 2;
		static constexpr uint32_t HangUp = 1u << 3;
	};

	struct WatchEntry
	{
#if defined(_WIN32)
		// IOCP 는 zero-byte WSARecv/WSASend 가 완료(GQCS 로 회수)되기 전까지 overlapped 를
		// 커널이 계속 참조한다. 그 전에 재사용(재무장)하거나 엔트리를 해제하면 각각 메모리
		// 오염 / use-after-free 가 되므로, pending 중 재등록·해제 요청은 즉시 처리하지 않고
		// pendingAction 에 적어 두었다가 실제 완료가 RunOnce 로 돌아온 뒤 처리한다.
		enum class PendingAction : uint8_t
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
		uint32_t generation{};
#endif
		uint32_t events{};
		IoCallback callback;
	};

	// fd 하나에 대한 Read 방향과 Write 방향 감시를 서로 독립된 WatchEntry 로 보관한다.
	// 예: 한 코루틴이 recv 를 기다리는 동안 다른 코루틴이 같은 fd 로 send 를 기다릴 수 있는데,
	// 두 감시가 하나의 엔트리를 공유하면 나중 Watch 호출이 먼저 것의 콜백/제출 상태를 덮어써
	// 버린다. Slot() 은 이벤트 마스크의 Write 비트로 방향을 정한다(Read/Write 를 한 Watch 호출에
	// 같이 요청하는 caller 는 없음 — RecvAwaitable/SendAwaitable 등은 항상 한쪽만 요청한다).
	struct WatchSlots
	{
		WatchEntry read;
		WatchEntry write;

		[[nodiscard]] WatchEntry& Slot(const uint32_t _events) noexcept { return (_events & IoEvent::Write) ? write : read; }
		[[nodiscard]] bool_t Empty() const noexcept { return !read.callback && !write.callback; }
	};

	// 통합 비동기 완료 컨텍스트. 소켓 proactor + 파일 proactor 모두 사용.
	// 엔진은 완료 시 result 를 채운 뒤 handle.resume() 을 호출한다.
	// Windows: OVERLAPPED 가 반드시 첫 멤버여야 GQCS reinterpret_cast 가 성립한다.
	struct IoContext
	{
#if defined(_WIN32)
		OVERLAPPED overlapped{};
#endif
		std::coroutine_handle<> handle;
		ne::Result<std::size_t, ne::OsError> result{ ne::Result<std::size_t, ne::OsError>::Ok(0) };
	};



	// I/O 엔진 추상 인터페이스 — Reactor(Watch/Unwatch) + 소켓 Proactor(SubmitSend/SubmitReceive).
	// 구현체: EpollEngine (Linux), IocpEngine (Windows), IoUringEngine (Linux)
	//
	// 파일 I/O(SubmitRead/SubmitWrite, file_t 기반)는 의도적으로 이 인터페이스에 넣지 않는다.
	// epoll은 일반 파일에 대해 의미 있는 준비완료 알림을 주지 못해 EpollEngine이 파일 I/O를
	// 구현할 방법이 없다 — 순수가상으로 두면 EpollEngine이 추상 클래스가 되어 인스턴스화조차
	// 안 되거나, 억지로 NotSupported 스텁을 넣어야 한다(런타임까지 안 되는지 모르는 함정).
	// 대신 파일 I/O는 IoUringEngine / IocpEngine 각각의 비가상 멤버로만 제공하고,
	// FileReadAwaitable / FileWriteAwaitable 이 IIoEngine& 가 아닌 구체 엔진 타입을 참조한다.
	class IIoEngine
	{
	public:
		IIoEngine() = default;
		virtual ~IIoEngine() = default;

		NEBULA_NON_COPYABLE_MOVABLE(IIoEngine)

	public: // Reactor
		// _events 는 Read 또는 Write 중 하나의 방향을 나타낸다(둘을 한 호출에 같이 요청하지 않음) —
		// 같은 fd 에 대해 두 방향을 독립적으로 동시에 Watch 할 수 있다.
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Watch(socket_t _fd, uint32_t _events, IoCallback _callback) = 0;
		// _events 로 해제할 방향을 지정한다 — 기본값(Read|Write)은 두 방향 모두 해제(전체 해제, 예: 소켓 Close).
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Unwatch(socket_t _fd, uint32_t _events = IoEvent::Read | IoEvent::Write) = 0;

	public: // 소켓 Proactor
		[[nodiscard]] virtual ne::Result<void, ne::OsError> SubmitSend(socket_t _fd, const void* _buffer, std::size_t _length, IoContext* _context) noexcept = 0;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> SubmitReceive(socket_t _fd, void* _buffer, std::size_t _length, IoContext* _context) noexcept = 0;

	public:
		[[nodiscard]] virtual ne::Result<void, ne::OsError> RunOnce(ne::int_t _timeoutMs = -1) = 0;
		virtual void SetTimerWheel(ne::time::TimerWheel* _timerWheel) noexcept = 0;
	};

END_NS
