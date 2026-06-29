//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstdint>
#include <functional>
#include "NetworkType.h"
#include "Result.h"
#include "Error.h"

BEGIN_NS(ne::network)
	// I/O 이벤트 플래그 (플랫폼 중립적)
	struct IoEvent
	{
		static constexpr uint32_t Read = 1u << 0;
		static constexpr uint32_t Write = 1u << 1;
		static constexpr uint32_t Error = 1u << 2;
		static constexpr uint32_t HangUp = 1u << 3;
	};

	// fd + 발생 이벤트 플래그를 받는 콜백
	using IoCallback = std::function<void(socket_t _fd, uint32_t _events)>;

	// I/O 엔진 추상 인터페이스 (reactor 패턴)
	//   구현체: EpollEngine (Linux), IoUringEngine (Linux), IocpEngine (Windows)
	class IIoEngine
	{
		NEBULA_NON_COPYABLE_MOVABLE(IIoEngine)

	public:
		IIoEngine() = default;
		virtual ~IIoEngine() = default;

	public:
		// fd를 감시 대상에 등록. 이미 등록된 fd면 이벤트 마스크와 콜백을 교체.
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Watch(socket_t _fd, uint32_t _events, IoCallback _cb) = 0;
		// fd를 감시 대상에서 제거.
		[[nodiscard]] virtual ne::Result<void, ne::OsError> Unwatch(socket_t _fd) = 0;
		// 준비된 이벤트를 최대 한 번 처리. _timeoutMs = -1 이면 무기한 대기.
		[[nodiscard]] virtual ne::Result<void, ne::OsError> RunOnce(int_t _timeoutMs = -1) = 0;
	};

END_NS
