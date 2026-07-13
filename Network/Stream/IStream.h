//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <stop_token>
#include "Base/Coroutine/Task.h"
#include "Base/Result.h"
#include "Base/Error.h"
#include "Base/Type.h"
#include "Io/Buffer/BufferView.h"
#include "Io/Buffer/BufferChain.h"
#include "Io/IoError.h"
#include "Io/IoResult.h"

BEGIN_NS (ne::network)
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
	virtual Task<ne::io::IoResult<void_t>> Handshake(std::stop_token = {}) = 0;
	virtual Task<ne::io::IoResult<size_t>> Receive(ne::io::BufferView, std::stop_token = {}) = 0;
	virtual Task<ne::io::IoResult<size_t>> Receivev(const ne::io::BufferChain&, std::stop_token = {}) = 0;
	virtual Task<ne::io::IoResult<size_t>> Send(ne::io::BufferView, std::stop_token = {}) = 0;
	virtual Task<ne::io::IoResult<size_t>> Sendv(const ne::io::BufferChain&, std::stop_token = {}) = 0;
	virtual Task<ne::io::IoResult<void_t>> Shutdown() = 0;
	virtual Result<void_t, ne::io::IoError> Close() = 0;

public:
	[[nodiscard]] virtual bool_t IsOpen() const noexcept = 0;
};

END_NS
