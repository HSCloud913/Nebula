//
// Created by hscloud on 26. 6. 30.
//

#include "BufferBlock.h"



BEGIN_NS(ne::network)
	BufferBlock::BufferBlock(ne::byte_t* _data, const std::size_t _capacity, ne::memory::IAllocator* _pool) noexcept
		: data(_data)
		, capacity(_capacity)
		, pool(_pool) {}



	ne::Result<BufferBlock*, ne::OsError> BufferBlock::Acquire(ne::memory::IAllocator& _pool, std::size_t _size, std::size_t _align) noexcept
	{
		void* memory = _pool.Allocate(_size + sizeof(BufferBlock), _align);
		if (!memory)
			return ne::Result<BufferBlock*, ne::OsError>::Error(
				ne::OsError{ 0, "BufferBlock: pool exhausted" }.Context("[BufferBlock/Acquire]"));

		auto* buffer = static_cast<BufferBlock*>(memory);
		auto* data = reinterpret_cast<ne::byte_t*>(buffer + 1);

		new(buffer) BufferBlock(data, _size, &_pool);

		return ne::Result<BufferBlock*, ne::OsError>::Ok(buffer);
	}



	void BufferBlock::Release() noexcept
	{
		if (refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			auto* tempPool = pool;
			const std::size_t size = capacity + sizeof(BufferBlock);

			this->~BufferBlock();

			tempPool->Deallocate(this, size);
		}
	}
END_NS
