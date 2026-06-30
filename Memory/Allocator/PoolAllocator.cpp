//
// Created by hscloud on 26. 6. 30.
//

#include "PoolAllocator.h"

#include <cassert>



BEGIN_NS(ne::memory)
	PoolAllocator::PoolAllocator(const std::size_t _blockSize, const std::size_t _blockCount)
		: blockSize(std::max(_blockSize, sizeof(FreeNode)))
		, blockCount(_blockCount)
		, available(_blockCount)
	{
		pool = static_cast<ne::byte_t*>(std::malloc(blockSize * _blockCount));
		assert(pool && "PoolAllocator: allocation failed");

		freeList = nullptr;
		for (std::size_t i = blockCount; i-- > 0;)
		{
			auto* node = reinterpret_cast<FreeNode*>(pool + i * blockSize);
			node->next = freeList;
			freeList = node;
		}
	}

	PoolAllocator::~PoolAllocator()
	{
		std::free(pool);
	}



	void* PoolAllocator::Allocate(std::size_t _size, std::size_t)
	{
		assert(_size <= blockSize && "PoolAllocator: requested size exceeds block size");

		std::lock_guard<std::mutex> lock(mutex);
		if (!freeList) return nullptr;

		FreeNode* node = freeList;
		freeList = node->next;
		--available;

		return node;
	}

	void PoolAllocator::Deallocate(void* _ptr, std::size_t) noexcept
	{
		if (!_ptr) return;

		auto* node = static_cast<FreeNode*>(_ptr);

		std::lock_guard<std::mutex> lock(mutex);
		node->next = freeList;
		freeList = node;
		++available;
	}

	std::size_t PoolAllocator::Available() const noexcept
	{
		std::lock_guard<std::mutex> lock(mutex);

		return available;
	}
END_NS
