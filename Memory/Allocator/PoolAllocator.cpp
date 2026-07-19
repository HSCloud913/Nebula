//
// Created by hscloud on 26. 6. 30.
//

#include "Memory/Allocator/PoolAllocator.h"

#include <algorithm>
#include <cstdlib>
#include <limits>



namespace
{
	[[nodiscard]] std::size_t RoundUp(const std::size_t _value, const std::size_t _multiple) noexcept { return (_value + _multiple - 1) / _multiple * _multiple; }

	[[nodiscard]] ne::void_t* AlignedAlloc(const std::size_t _alignment, const std::size_t _size) noexcept
	{
#if defined(_WIN32)
		return ::_aligned_malloc(_size, _alignment);
#elif defined(IS_POSIX)
		return std::aligned_alloc(_alignment, _size);
#endif
	}

	ne::void_t AlignedFree(ne::void_t* _ptr) noexcept
	{
#if defined(_WIN32)
		::_aligned_free(_ptr);
#elif defined(IS_POSIX)
		std::free(_ptr);
#endif
	}
}



BEGIN_NS(ne::memory)
	PoolAllocator::PoolAllocator(const std::size_t _blockSize, const std::size_t _blockCount, const std::size_t _alignment)
		: alignment(std::max(_alignment, alignof(FreeNode)))
		, blockSize(RoundUp(std::max(_blockSize, sizeof(FreeNode)), alignment)) // alignment 의 배수로 올림 → 모든 블록 정렬 보장
		, blockCount(_blockCount)
		, available(0)
	{
		// 곱셈 오버플로 검사 — 초과하면 할당하지 않는다. (pool == nullptr → Allocate 는 값으로 nullptr 반환)
		if (_blockCount != 0 && blockSize <= (std::numeric_limits<std::size_t>::max)() / _blockCount)
			pool = static_cast<ne::byte_t*>(AlignedAlloc(alignment, blockSize * _blockCount));

		if (pool == nullptr) return; // available 은 이미 0, head 는 기본값(InvalidIndex)이라 빈 풀 상태로 남는다.

		// 인덱스 0 → 1 → ... → (blockCount-1) → InvalidIndex 순으로 연결하고, head 를 0 으로 둔다.
		// 이 시점엔 다른 스레드가 this 를 아직 볼 수 없으므로 plain 하게 채워도 안전하다.
		for (std::size_t i = 0; i < _blockCount; ++i)
		{
			const std::uint32_t next = (i + 1 == _blockCount) ? InvalidIndex : static_cast<std::uint32_t>(i + 1);
			NodeAt(static_cast<std::uint32_t>(i))->next.store(next, std::memory_order_relaxed);
		}

		head.store(Pack({ 0, 0 }), std::memory_order_relaxed);
		available.store(_blockCount, std::memory_order_relaxed);
	}

	PoolAllocator::~PoolAllocator() { AlignedFree(pool); }



	void_t* PoolAllocator::Allocate(const std::size_t _size, const std::size_t _align)
	{
		// 블록 크기 초과 또는 풀 정렬을 넘는 요청은 값으로 실패(nullptr) — 버퍼 오버런/오정렬 UB 방지.
		if (_size > blockSize || _align > alignment) return nullptr;

		// Treiber 스택 pop: head 를 읽고, 그 노드의 next 를 새 head 후보로 삼아 CAS 로 교체한다.
		// CAS 가 실패하면(그 사이 다른 스레드가 먼저 pop/push 함) old 는 최신값으로 자동 갱신되어 재시도된다.
		// tag 를 pop 마다 증가시키므로, 같은 index 가 되돌아와도(ABA) CAS 는 옛 tag 로는 성공하지 못한다.
		std::uint64_t oldPacked = head.load(std::memory_order_acquire);
		while (true)
		{
			const auto [index, tag] = Unpack(oldPacked);
			if (index == InvalidIndex) return nullptr; // 풀 고갈

			FreeNode* node = NodeAt(index);
			const std::uint32_t nextIndex = node->next.load(std::memory_order_relaxed);
			const std::uint64_t newPacked = Pack({ nextIndex, tag + 1 });

			if (head.compare_exchange_weak(oldPacked, newPacked, std::memory_order_acquire, std::memory_order_relaxed))
			{
				available.fetch_sub(1, std::memory_order_relaxed);
				return node;
			}
		}
	}

	void_t PoolAllocator::Deallocate(void_t* _ptr, std::size_t) noexcept
	{
		if (!_ptr) return;

		auto* node = static_cast<FreeNode*>(_ptr);
		const auto index = static_cast<std::uint32_t>((reinterpret_cast<ne::byte_t*>(node) - pool) / blockSize);

		// Treiber 스택 push: 이 노드를 현재 head 앞에 연결한 새 head 후보를 CAS 로 밀어넣는다.
		// pop 과 마찬가지로 tag 를 증가시켜, 이 노드가 다시 pop 됐다가 재차 push 되는 ABA 상황과 구분한다.
		std::uint64_t oldPacked = head.load(std::memory_order_relaxed);
		while (true)
		{
			const auto [idx, tag] = Unpack(oldPacked);
			node->next.store(idx, std::memory_order_relaxed);
			const std::uint64_t newPacked = Pack({ index, tag + 1 });

			if (head.compare_exchange_weak(oldPacked, newPacked, std::memory_order_release, std::memory_order_relaxed)) break;
		}

		available.fetch_add(1, std::memory_order_relaxed);
	}

END_NS
