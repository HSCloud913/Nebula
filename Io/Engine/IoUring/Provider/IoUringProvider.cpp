//
// Created by hscloud on 26. 7. 8.
//

#include "Io/Engine/IoUring/Provider/IoUringProvider.h"

#if defined(IS_POSIX)
#include "Base/Error.h"



BEGIN_NS(ne::io)
	ne::Result<BufferHandle, IoError> IoUringProvider::RegisterBuffer(const std::span<ne::byte_t> _region) noexcept
	{
		if (_region.empty())
			return ne::Result<BufferHandle, IoError>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "empty region" });

		std::lock_guard lock(mutex);

		if (!EnsureSparseRegisteredLocked())
			return ne::Result<BufferHandle, IoError>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[IoUringProvider/io_uring_register_buffers_sparse]"));

		// 선형 탐색으로 비어 있는 슬롯을 찾는다. MaxBuffers 가 크지 않고 등록/해제가 빈번한 핫패스가
		// 아니므로 별도의 free-list 없이 단순 탐색으로 충분하다고 판단한 설계다.
		uint_t slot = MaxBuffers;
		for (uint_t i = 0; i < MaxBuffers; ++i)
		{
			if (!usedSlots[i])
			{
				slot = i;
				break;
			}
		}

		if (slot == MaxBuffers)
			return ne::Result<BufferHandle, IoError>::Error(IoError{ IoErrorKind::REGISTRATION_LIMIT_EXCEEDED });

		// update_tag 는 sparse 로 예약해 둔 슬롯 하나만 골라 실제 iovec 을 채워 넣는 갱신 API 다.
		iovec iov{ _region.data(), _region.size() };
		__u64 tag = 0;
		if (::io_uring_register_buffers_update_tag(ring, slot, &iov, &tag, 1) != 0)
			return ne::Result<BufferHandle, IoError>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[IoUringProvider/io_uring_register_buffers_update_tag]"));

		usedSlots[slot] = true;
		// 0은 BufferHandle::IsValid() 기준 "무효" 이므로, 실제 슬롯 인덱스에 1을 더해 핸들 값으로 사용한다.
		return ne::Result<BufferHandle, IoError>::Ok(BufferHandle{ static_cast<uint64_t>(slot) + 1 });
	}

	void_t IoUringProvider::UnregisterBuffer(const BufferHandle _handle) noexcept
	{
		if (!_handle.IsValid()) return;

		std::lock_guard lock(mutex);
		if (!isSparseRegistered) return;

		// 핸들 값은 "슬롯 인덱스 + 1" 로 발급했으므로 되돌려서 실제 인덱스를 구한다.
		const uint64_t slot = _handle.value - 1;
		if (slot >= MaxBuffers || !usedSlots[slot]) return;

		// 빈 iovec 으로 update_tag 를 호출해 해당 슬롯을 다시 sparse(비어 있는) 상태로 되돌린다.
		iovec empty{ nullptr, 0 };
		__u64 tag = 0;
		(void_t)::io_uring_register_buffers_update_tag(ring, static_cast<uint_t>(slot), &empty, &tag, 1);
		usedSlots[slot] = false;
	}



	bool_t IoUringProvider::EnsureSparseRegisteredLocked() noexcept
	{
		if (isSparseRegistered) return true;

		// sparse 등록은 슬롯 개수만 지정해 빈 테이블을 미리 만들어 두는 1회성 초기화이며,
		// 이후 개별 슬롯은 update_tag 로 채우거나 비운다.
		if (::io_uring_register_buffers_sparse(ring, MaxBuffers) != 0) return false;
		isSparseRegistered = true;

		return true;
	}
END_NS

#endif // IS_POSIX
