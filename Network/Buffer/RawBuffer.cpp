//
// Created by hscloud on 26. 6. 30.
//

#include "RawBuffer.h"



BEGIN_NS(ne::network)
	RawBuffer::RawBuffer(ne::byte_t* _data, const std::size_t _capacity, ne::memory::IAllocator* _pool) noexcept
		: data(_data)
		, capacity(_capacity)
		, pool(_pool) {}



	ne::Result<RawBuffer*, ne::OsError> RawBuffer::Acquire(ne::memory::IAllocator& _pool, std::size_t _size, std::size_t _align) noexcept
	{
		void* memory = _pool.Allocate(_size + sizeof(RawBuffer), _align);
		if (!memory)
			return ne::Result<RawBuffer*, ne::OsError>::Error(
				ne::OsError{ 0, "RawBuffer: pool exhausted" }.Context("[RawBuffer/Acquire]"));

		auto* buffer = static_cast<RawBuffer*>(memory);
		auto* data = reinterpret_cast<ne::byte_t*>(buffer + 1);

		new(buffer) RawBuffer(data, _size, &_pool); // 미리 allocated 된 공간에 RawBuffer 생성 요청

		return ne::Result<RawBuffer*, ne::OsError>::Ok(buffer);
	}



	void RawBuffer::Release() noexcept
	{
		if (refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			auto* tempPool = pool;
			const std::size_t size = capacity + sizeof(RawBuffer);

			this->~RawBuffer();

			tempPool->Deallocate(this, size);
		}
	}
END_NS
