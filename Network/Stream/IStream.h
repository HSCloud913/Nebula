//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cstddef>
#include "Base/Coroutine/Task.h"
#include "Base/Result.h"
#include "Base/Error.h"
#include "Base/Type.h"
#include "Io/Buffer/BufferView.h"
#include "Io/Buffer/BufferChain.h"

BEGIN_NS(ne::network)
	// 바이트 스트림 추상 인터페이스 (async-only).
	// 모든 I/O 는 코루틴 반환 — co_await 로 비동기 완료 대기.
	// 반환값 size_t == 0 → 상대방이 연결을 닫음 (EOF).
	class IStream
	{
	public:
		IStream() = default;
		virtual ~IStream() = default;

		NEBULA_NON_COPYABLE_MOVABLE(IStream)

	public:
		virtual ne::Task<ne::Result<void, ne::OsError>> Handshake() = 0;
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Send(ne::io::BufferView _data) = 0;
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Sendv(const ne::io::BufferChain& _chain) = 0;
		virtual ne::Task<ne::Result<std::size_t, ne::OsError>> Receive(ne::io::BufferView _data) = 0;
		virtual ne::Task<ne::Result<void, ne::OsError>> Shutdown() = 0;
		virtual ne::Result<void, ne::OsError> Close() = 0;

	public:
		[[nodiscard]] virtual bool_t IsOpen() const noexcept = 0;
	};

END_NS
