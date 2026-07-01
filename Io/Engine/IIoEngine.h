//
// Created by hscloud on 26. 7. 1.
//

#pragma once
#include <concepts>
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
	struct IoEvent
	{
		static constexpr uint32_t Read = 1u << 0;
		static constexpr uint32_t Write = 1u << 1;
		static constexpr uint32_t Error = 1u << 2;
		static constexpr uint32_t HangUp = 1u << 3;
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

	template <typename T>
	concept ValidHandleType = std::same_as<T, socket_t> || std::same_as<T, file_t>;

	template <ValidHandleType HandleType>
	using IoCallback = std::function<void(HandleType _handle, uint32_t _events)>;

	// I/O 엔진 추상 인터페이스 — Reactor(Watch/Unwatch) + Proactor(SubmitRecv/SubmitSend).
	// T: fd 타입. 기본값 socket_t (플랫폼별 SOCKET / int).
	// 구현체: EpollEngine (Linux), IocpEngine (Windows), IoUringEngine (Linux)
	template <ValidHandleType HandleType>
	class IIoEngine
	{
	public:
		IIoEngine() = default;
		virtual ~IIoEngine() = default;

		NEBULA_NON_COPYABLE_MOVABLE(IIoEngine)

	public: // Reactor
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Watch(HandleType _handle, uint32_t _events, IoCallback<HandleType> _callback) = 0;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Unwatch(HandleType _handle) = 0;

	public: // Proactor
		[[nodiscard]] virtual ne::Result<void, ne::OsError> SubmitSend(HandleType _handle, const void* _buffer, std::size_t _length, IoContext* _context) noexcept = 0;
		[[nodiscard]] virtual ne::Result<void, ne::OsError> SubmitReceive(HandleType _handle, void* _buffer, std::size_t _length, IoContext* _context) noexcept = 0;

	public:
		[[nodiscard]] virtual ne::Result<void, ne::OsError> RunOnce(ne::int_t _timeoutMs = -1) = 0;
		virtual void SetTimerWheel(ne::time::TimerWheel* _timerWheel) noexcept = 0;
	};

END_NS
