//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstddef>
#include <mutex>
#include "IAllocator.h"
#include "Type.h"

BEGIN_NS(ne::memory)
	// 고정 크기 블록 풀. free list 기반.
	// 스레드 안전 (mutex 보호, 추후 lock-free 교체 예정).
	class PoolAllocator final :public IAllocator
	{
	public:
		NEBULA_NON_COPYABLE_MOVABLE(PoolAllocator)

		PoolAllocator(std::size_t _blockSize, std::size_t _blockCount);
		~PoolAllocator() override;

	private:
		struct FreeNode
		{
			FreeNode* next;
		};

		std::size_t blockSize;
		std::size_t blockCount;
		std::size_t available;
		ne::byte_t* pool{ static_cast<ne::byte_t*>(nullptr) };
		FreeNode* freeList{ static_cast<FreeNode*>(nullptr) };
		mutable std::mutex mutex;

	public:
		[[nodiscard]] virtual void* Allocate(std::size_t _size, std::size_t _align) override;
		virtual void Deallocate(void* _ptr, std::size_t _size) noexcept override;
		[[nodiscard]] virtual std::size_t Available() const noexcept override;
	};

END_NS
