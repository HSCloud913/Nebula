//
// Created by hscloud on 26. 6. 30.
//

#include "Memory/Allocator/PoolAllocator.h"

#include <algorithm>
#include <cstdlib>
#include <limits>



BEGIN_NS(ne::memory)
	namespace
	{
		[[nodiscard]] std::size_t RoundUp(const std::size_t _value, const std::size_t _multiple) noexcept { return (_value + _multiple - 1) / _multiple * _multiple; }

		[[nodiscard]] void_t* AlignedAlloc(const std::size_t _alignment, const std::size_t _size) noexcept
		{
#if defined(_WIN32)
			return ::_aligned_malloc(_size, _alignment);
#elif defined(IS_POSIX)
			return std::aligned_alloc(_alignment, _size); // _size 는 _alignment 의 배수여야 함(호출측이 보장)
#endif
		}

		void_t AlignedFree(void_t* _ptr) noexcept
		{
#if defined(_WIN32)
			::_aligned_free(_ptr);
#elif defined(IS_POSIX)
			std::free(_ptr);
#endif
		}
	}

	PoolAllocator::PoolAllocator(const std::size_t _blockSize, const std::size_t _blockCount, const std::size_t _alignment)
		: alignment(std::max(_alignment, alignof(FreeNode)))
		, blockSize(RoundUp(std::max(_blockSize, sizeof(FreeNode)), alignment)) // alignment 의 배수로 올림 → 모든 블록 정렬 보장
		, blockCount(_blockCount)
		, available(_blockCount)
	{
		// 곱셈 오버플로 검사 — 초과하면 할당하지 않는다(pool == nullptr → Allocate 는 값으로 nullptr 반환).
		if (_blockCount != 0 && blockSize <= (std::numeric_limits<std::size_t>::max)() / _blockCount) pool = static_cast<ne::byte_t*>(AlignedAlloc(alignment, blockSize * _blockCount));

		if (pool == nullptr)
		{
			available = 0;
			return;
		}

		freeList = nullptr;
		for (std::size_t i = _blockCount; i-- > 0;)
		{
			auto* node = reinterpret_cast<FreeNode*>(pool + i * blockSize);
			node->next = freeList;
			freeList = node;
		}
	}

	PoolAllocator::~PoolAllocator() { AlignedFree(pool); }



	void_t* PoolAllocator::Allocate(const std::size_t _size, const std::size_t _align)
	{
		// 블록 크기 초과 또는 풀 정렬을 넘는 요청은 값으로 실패(nullptr) — 버퍼 오버런/오정렬 UB 방지.
		if (_size > blockSize || _align > alignment) return nullptr;

		std::lock_guard<std::mutex> lock(mutex);
		if (!freeList) return nullptr;

		FreeNode* node = freeList;
		freeList = node->next;
		--available;

		return node;
	}

	void_t PoolAllocator::Deallocate(void_t* _ptr, std::size_t) noexcept
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
