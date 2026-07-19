//
// Created by hscloud on 26. 7. 8.
//

#pragma once
#include "Base/Type.h"
#if defined(IS_POSIX)

#include "Io/Engine/IRegisteredBufferProvider.h"
#include <liburing.h>
#include <mutex>
#include <vector>

BEGIN_NS(ne::io)
	/**
	 * @class IoUringProvider
	 * @brief io_uring 등록 버퍼(Fixed Buffer) provider.
	 *
	 * IoUringEngine 이 소유(합성)하며, RegisterBuffer/UnregisterBuffer 로 sparse 버퍼 테이블의
	 * 슬롯을 관리하는 것이 실질적인 역할이다. RIO 와 달리 io_uring 의 고정 버퍼는 별도 제출 API 가
	 * 없고 IoUringEngine::Submit() 의 RequestKind::READ_FIXED/WRITE_FIXED 가 슬롯 인덱스를 SQE 의
	 * buf_index 로 직접 넘기므로, SubmitSendRegistered/SubmitReceiveRegistered 는 이 provider
	 * 에서 호출 경로가 없어 항상 UNSUPPORTED 를 반환한다.
	 *
	 * @note io_uring_register_buffers_sparse/io_uring_register_buffers_update_tag 는 liburing
	 * 2.2(커널 5.13+) 이상이 필요하다.
	 */
	class IoUringProvider final :public IRegisteredBufferProvider
	{
	public:
		/**
		 * @brief 등록 버퍼를 관리할 대상 io_uring 링을 저장한다(실제 sparse 등록은 최초 사용 시점까지 지연).
		 * @param _ring IoUringEngine 이 소유한 링에 대한 포인터. provider 는 이 링의 생명주기 동안만 유효하다.
		 */
		explicit IoUringProvider(io_uring* _ring) noexcept
			: ring(_ring) {}

		/** @brief 별도로 해제할 자원이 없다(등록된 버퍼 슬롯은 io_uring 링 자체가 정리될 때 함께 사라짐). */
		virtual ~IoUringProvider() override = default;

		NEBULA_NON_COPYABLE_MOVABLE(IoUringProvider)

	private:
		static constexpr uint_t MaxBuffers = 1024;

	private:
		io_uring* ring;
		std::mutex mutex;
		bool_t isSparseRegistered{ false };
		std::vector<bool_t> usedSlots{ std::vector<bool_t>(MaxBuffers, false) };

	public: /* IRegisteredBufferProvider */
		/**
		 * @brief sparse 버퍼 테이블에서 빈 슬롯을 찾아 해당 인덱스에 _region 을 등록한다.
		 *
		 * io_uring 의 고정 버퍼는 RIO 처럼 개별 등록이 아니라 "테이블(슬롯 배열)" 개념이라, 먼저
		 * EnsureSparseRegisteredLocked() 로 MaxBuffers 크기의 빈 테이블을 한 번 만들어 두고,
		 * 이후 io_uring_register_buffers_update_tag 로 빈 슬롯 하나만 갱신하는 방식으로 개별 등록을
		 * 흉내낸다. 반환되는 BufferHandle 값은 "슬롯 인덱스 + 1"(0은 무효 핸들이므로)이다.
		 *
		 * @param _region 등록할 영역(비어 있으면 실패).
		 * @return 성공 시 BufferHandle, 슬롯이 모두 찼거나(REGISTRATION_LIMIT_EXCEEDED) API 실패 시 IoError.
		 */
		[[nodiscard]] virtual ne::Result<BufferHandle, IoError> RegisterBuffer(std::span<ne::byte_t> _region) noexcept override;

		/**
		 * @brief 해당 슬롯을 빈 iovec 으로 갱신해(update_tag) 사실상 등록 해제하고 usedSlots 를 비운다.
		 * @param _handle RegisterBuffer() 가 반환한 핸들(값 - 1 이 실제 슬롯 인덱스). 무효/범위 밖/미사용 슬롯은 무시.
		 */
		virtual void_t UnregisterBuffer(BufferHandle _handle) noexcept override;

	public: /* IRegisteredBufferProvider */
		/**
		 * @brief 항상 UNSUPPORTED 를 반환한다.
		 *
		 * @note io_uring 고정 버퍼는 IoUringEngine::Submit() 의 READ_FIXED/WRITE_FIXED 가 buf_index 를
		 * SQE 에 직접 넘기는 방식으로만 소비되며, RIO 처럼 provider 를 통한 별도 제출 경로가 없다.
		 */
		[[nodiscard]] virtual ne::Result<void_t, IoError> SubmitSendRegistered(socket_t _socket, BufferHandle _handle, const void_t* _buffer, std::size_t _length, void_t* _userData) noexcept override { return ne::Result<void_t, IoError>::Error(IoError{ IoErrorKind::UNSUPPORTED, "io_uring fixed buffers are consumed via ReadFixed/WriteFixed, not this method" }); }
		/** @brief 항상 UNSUPPORTED 를 반환한다(SubmitSendRegistered 와 동일한 이유). */
		[[nodiscard]] virtual ne::Result<void_t, IoError> SubmitReceiveRegistered(socket_t _socket, BufferHandle _handle, void_t* _buffer, std::size_t _length, void_t* _userData) noexcept override { return ne::Result<void_t, IoError>::Error(IoError{ IoErrorKind::UNSUPPORTED, "io_uring fixed buffers are consumed via ReadFixed/WriteFixed, not this method" }); }

	private:
		/**
		 * @brief io_uring_register_buffers_sparse 로 MaxBuffers 크기의 빈 고정 버퍼 테이블을 최초 1회 생성한다.
		 *
		 * sparse 등록은 각 슬롯을 비어 있는 상태로 예약만 해두고, 실제 iovec 은 이후
		 * io_uring_register_buffers_update_tag 로 슬롯 단위로 채우거나 비우는 최신(liburing 2.2+)
		 * API 조합이다. 이렇게 하면 버퍼 하나가 추가/제거될 때마다 테이블 전체를 재등록할 필요가 없다.
		 * @return 이미 등록되어 있거나 새로 등록에 성공하면 true.
		 * @note 호출 전 mutex 가 잠겨 있어야 한다.
		 */
		[[nodiscard]] bool_t EnsureSparseRegisteredLocked() noexcept;
	};

END_NS

#endif // IS_POSIX
