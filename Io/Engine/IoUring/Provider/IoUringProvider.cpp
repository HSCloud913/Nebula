//
// Created by hscloud on 26. 7. 8.
//

#include "Io/Engine/IoUring/Provider/IoUringProvider.h"

#if defined(IS_POSIX)
#include "Base/Error.h"

BEGIN_NS(ne::io)
	bool_t IoUringProvider::EnsureSparseRegisteredLocked() noexcept
	{
		if (isSparseRegistered) return true;

		if (::io_uring_register_buffers_sparse(ring, MaxBuffers) != 0) return false;
		isSparseRegistered = true;
		return true;
	}

	ne::Result<BufferHandle, IoError> IoUringProvider::RegisterBuffer(const std::span<ne::byte_t> _region) noexcept
	{
		if (_region.empty()) return ne::Result<BufferHandle, IoError>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "empty region" });

		std::lock_guard lock(mutex);

		if (!EnsureSparseRegisteredLocked())
			return ne::Result<BufferHandle, IoError>::Error(
				IoError{ ne::OsError{ ne::LastOsError() } }.Context("[IoUringProvider/io_uring_register_buffers_sparse]"));

		uint_t slot = MaxBuffers;
		for (uint_t i = 0; i < MaxBuffers; ++i)
			if (!usedSlots[i]) { slot = i; break; }

		if (slot == MaxBuffers)
			return ne::Result<BufferHandle, IoError>::Error(IoError{ IoErrorKind::REGISTRATION_LIMIT_EXCEEDED });

		iovec iov{ _region.data(), _region.size() };
		__u64 tag = 0;
		if (::io_uring_register_buffers_update_tag(ring, slot, &iov, &tag, 1) != 0)
			return ne::Result<BufferHandle, IoError>::Error(
				IoError{ ne::OsError{ ne::LastOsError() } }.Context("[IoUringProvider/io_uring_register_buffers_update_tag]"));

		usedSlots[slot] = true;
		// 슬롯 인덱스를 그대로 handle 값으로 쓰되 0(무효)과 겹치지 않도록 +1 — Submit()의
		// ReadFixed/WriteFixed 가 bufferId 를 buf_index 로 넘길 때 다시 -1 한다.
		return ne::Result<BufferHandle, IoError>::Ok(BufferHandle{ static_cast<uint64_t>(slot) + 1 });
	}

	void_t IoUringProvider::UnregisterBuffer(const BufferHandle _handle) noexcept
	{
		if (!_handle.IsValid()) return;

		std::lock_guard lock(mutex);
		if (!isSparseRegistered) return;

		const uint64_t slot = _handle.value - 1;
		if (slot >= MaxBuffers || !usedSlots[slot]) return;

		iovec empty{ nullptr, 0 };
		__u64 tag = 0;
		(void_t)::io_uring_register_buffers_update_tag(ring, static_cast<uint_t>(slot), &empty, &tag, 1);
		usedSlots[slot] = false;
	}

	ne::Result<void_t, ne::OsError> IoUringProvider::SubmitSendRegistered(socket_t, BufferHandle, const void_t*, std::size_t, void_t*) noexcept
	{
		// io_uring 고정 버퍼는 Submit()의 SQE(ReadFixed/WriteFixed) 로만 소비된다 — 이 경로는 없음.
		return ne::Result<void_t, ne::OsError>::Error(ne::OsError{ 0, "io_uring fixed buffers are consumed via ReadFixed/WriteFixed, not this method" });
	}

	ne::Result<void_t, ne::OsError> IoUringProvider::SubmitReceiveRegistered(socket_t, BufferHandle, void_t*, std::size_t, void_t*) noexcept
	{
		return ne::Result<void_t, ne::OsError>::Error(ne::OsError{ 0, "io_uring fixed buffers are consumed via ReadFixed/WriteFixed, not this method" });
	}
END_NS

#endif // IS_POSIX
