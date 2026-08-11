//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <stop_token>
#include "Base/Coroutine/Task.h"
#include "Base/Result.h"
#include "Base/Error.h"
#include "Base/Type.h"
#include "Memory/Buffer/BufferView.h"
#include "Memory/Buffer/BufferChain.h"
#include "Io/Diagnostic/Error.h"

namespace ne::network
{
	/**
	* @class IStream
	* @brief 바이트 스트림에 대한 비동기 전용(async-only) 추상 인터페이스입니다.
	*
	* 모든 I/O는 코루틴으로 반환되어 co_await로 완료를 기다립니다. Receive/Send 계열의
	* 반환값 size_t가 0이면 상대방이 연결을 닫았음(EOF)을 뜻합니다.
	*/
	class IStream
	{
	public:
		IStream() = default;
		virtual ~IStream() = default;

		NEBULA_NON_COPYABLE_MOVABLE(IStream)

	public:
		[[nodiscard]] virtual Task<ne::io::IoResult<void_t>> Handshake(std::stop_token = {}) = 0;
		[[nodiscard]] virtual Task<ne::io::IoResult<size_t>> Receive(ne::memory::BufferView, std::stop_token = {}) = 0;
		[[nodiscard]] virtual Task<ne::io::IoResult<size_t>> Receivev(const ne::memory::BufferChain&, std::stop_token = {}) = 0;
		[[nodiscard]] virtual Task<ne::io::IoResult<size_t>> Send(ne::memory::BufferView, std::stop_token = {}) = 0;
		[[nodiscard]] virtual Task<ne::io::IoResult<size_t>> Sendv(const ne::memory::BufferChain&, std::stop_token = {}) = 0;
		[[nodiscard]] virtual Task<ne::io::IoResult<void_t>> Shutdown() = 0;
		[[nodiscard]] virtual ne::io::IoResult<void_t> Close() = 0;

	public:
		[[nodiscard]] virtual bool_t IsOpen() const noexcept = 0;
	};
}
