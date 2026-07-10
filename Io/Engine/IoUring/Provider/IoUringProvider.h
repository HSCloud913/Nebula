//
// Created by hscloud on 26. 7. 8.
//
// io_uring 등록 버퍼(Fixed Buffer) provider. IoUringEngine 이 소유(합성)한다.
//
// RIO(RioProvider)와 달리 io_uring 의 고정 버퍼는 별도 제출 API 가 없다 — 등록해 둔 슬롯 인덱스를
// io_uring_prep_read_fixed/write_fixed 의 buf_index 인자로 그대로 넘기면 끝이고, 이건 이미
// IoUringEngine::Submit() 의 OpCode::ReadFixed/WriteFixed 처리에서 Request.bufferId 로 하고
// 있다. 따라서 이 provider 는 RegisterBuffer/UnregisterBuffer(슬롯 관리)만 실질적인 일을 하고,
// IRegisteredBufferProvider 의 SubmitSendRegistered/SubmitReceiveRegistered 는 io_uring 경로에서
// 쓰이지 않으므로 UNSUPPORTED 를 반환한다(그 두 메서드는 RIO 처럼 "제출 자체가 provider 를 통하는"
// 모델을 위한 것 — io_uring 은 SQE 기반이라 맞지 않는다).
//
// 주의(미검증): io_uring_register_buffers_sparse/io_uring_register_buffers_update_tag 는
// liburing >= 2.2(커널 5.13+)가 필요하다 — 이 세션은 Linux 툴체인이 없어 컴파일/실행 확인을 못 했다.

#pragma once
#include "Base/Type.h" // IS_POSIX 정의 — 아래 가드 전에 반드시 포함
#if defined(IS_POSIX)

#include "Io/Engine/IRegisteredBufferProvider.h"
#include <liburing.h>
#include <mutex>
#include <vector>

BEGIN_NS(ne::io)
	class IoUringProvider final :public IRegisteredBufferProvider
	{
	public:
		explicit IoUringProvider(io_uring* _ring) noexcept
			: ring(_ring) {}
		virtual ~IoUringProvider() override = default;

		NEBULA_NON_COPYABLE_MOVABLE(IoUringProvider)

	private:
		// 슬롯 상한(고정) — sparse 등록 시 한 번에 예약해 두는 크기. TODO: 워크로드 기반 산정.
		static constexpr uint_t MaxBuffers = 1024;

		io_uring* ring;
		std::mutex mutex;
		bool_t isSparseRegistered{ false };
		std::vector<bool_t> usedSlots{ std::vector<bool_t>(MaxBuffers, false) };

	public: // IRegisteredBufferProvider
		[[nodiscard]] virtual ne::Result<BufferHandle, IoError> RegisterBuffer(std::span<ne::byte_t> _region) noexcept override;
		virtual void_t UnregisterBuffer(BufferHandle _handle) noexcept override;

	public:
		// io_uring 고정 버퍼는 Submit()의 ReadFixed/WriteFixed(SQE, buf_index=bufferId)로만 쓰인다 —
		// 이 두 메서드는 이 provider 에서 호출 경로가 없다(RIO 전용 모델). 항상 실패 반환.
		[[nodiscard]] virtual ne::Result<void_t, IoError> SubmitSendRegistered(socket_t _socket, BufferHandle _handle, const void_t* _buffer, std::size_t _length, void_t* _userData) noexcept override;
		[[nodiscard]] virtual ne::Result<void_t, IoError> SubmitReceiveRegistered(socket_t _socket, BufferHandle _handle, void_t* _buffer, std::size_t _length, void_t* _userData) noexcept override;

	private:
		// lazy: 최초 RegisterBuffer 호출 때 sparse 테이블 예약. mutex 를 이미 쥔 상태로 호출.
		[[nodiscard]] bool_t EnsureSparseRegisteredLocked() noexcept;
	};

END_NS

#endif // IS_POSIX
