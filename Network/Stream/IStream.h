//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstddef>
#include <span>
#include "Coroutine/Task.h"
#include "Result.h"
#include "Error.h"
#include "Type.h"

BEGIN_NS(ne::network)
	// 바이트 스트림 추상 인터페이스.
	// Send/Recv 는 코루틴 반환 — co_await 로 비동기 완료 대기.
	// 반환값 size_t == 0 → 상대방이 연결을 닫음 (EOF).
	class IStream
	{
	public:
		NEBULA_NON_COPYABLE_MOVABLE(IStream)

		IStream() = default;
		virtual ~IStream() = default;

	public:
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Send(std::span<const ne::byte_t> _data) = 0;
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Receive(std::span<ne::byte_t> _data) = 0;
		virtual ne::Result<void, ne::OsError> Close() = 0;

	public:
		[[nodiscard]] virtual bool_t IsOpen() const noexcept = 0;
	};

END_NS
