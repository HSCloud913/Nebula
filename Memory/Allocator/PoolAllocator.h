//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include "Memory/Allocator/IAllocator.h"
#include "Base/Type.h"

BEGIN_NS(ne::memory)
	/**
	 * @class PoolAllocator
	 * @brief free list 기반의 고정 크기 블록 풀 할당자입니다.
	 *
	 * lock-free(태그드 인덱스 기반 Treiber 스택)로 동작해 스레드 안전합니다. 모든 블록은 생성 시
	 * 지정한 _alignment(2의 거듭제곱)로 정렬되며, 이는 io_uring fixed buffer / Windows
	 * unbuffered I/O의 섹터 정렬(512·4096) 요구를 만족시키기 위함입니다.
	 *
	 * @note free list는 노드를 포인터가 아니라 풀 내부 인덱스로 연결하고, head는 {index, tag}
	 * 를 하나의 64비트 원자값으로 묶어 CAS합니다. 포인터를 직접 태깅하려면 128비트 CAS가
	 * 필요하지만, 고정 크기 풀은 인덱스 범위가 blockCount로 제한되므로 32비트 인덱스 + 32비트
	 * 세대(tag) 조합만으로 ABA 문제를 방지할 수 있습니다.
	 */
	class PoolAllocator final :public IAllocator
	{
	public:
		explicit PoolAllocator(std::size_t _blockSize, std::size_t _blockCount, std::size_t _alignment = alignof(std::max_align_t));
		virtual ~PoolAllocator() override;

		NEBULA_NON_COPYABLE_MOVABLE(PoolAllocator)

	private:
		static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "PoolAllocator 는 항상 lock-free 인 64비트 CAS 를 전제로 설계됨");

		static constexpr std::uint32_t InvalidIndex = 0xFFFFFFFFu;

		// free list 한 칸을 가리키는 {인덱스, 세대(tag)} 쌍. head 전체를 하나의 64비트 원자값으로
		// 다뤄야 CAS로 원자적으로 교체할 수 있으므로, Pack/Unpack으로 묶었다 풀었다 한다.
		struct TaggedHead
		{
			std::uint32_t index;
			std::uint32_t tag;
		};

		struct FreeNode
		{
			std::atomic<std::uint32_t> next;
		};

	private:
		// 선언 순서 주의: alignment 가 blockSize 초기화에 쓰이므로 먼저 선언한다(멤버 초기화 순서 = 선언 순서).
		std::size_t alignment;
		std::size_t blockSize;
		std::size_t blockCount;
		std::atomic<std::size_t> available;
		ne::byte_t* pool{ nullptr };
		std::atomic<std::uint64_t> head{ Pack({ InvalidIndex, 0 }) };

	private:
		[[nodiscard]] FreeNode* NodeAt(const std::uint32_t _index) const noexcept { return reinterpret_cast<FreeNode*>(pool + static_cast<std::size_t>(_index) * blockSize); }

	public:
		/** @brief 블록 하나를 lock-free 로 꺼내 반환합니다. _size/_align 이 풀 규격을 넘거나 풀이 고갈되면 nullptr. */
		[[nodiscard]] virtual void_t* Allocate(std::size_t _size, std::size_t _align) override;

		/** @brief Allocate() 로 받은 블록을 lock-free 로 반납합니다. */
		virtual void_t Deallocate(void_t* _ptr, std::size_t _size) noexcept override;

		/** @brief 현재 비어있는(할당 가능한) 블록 수의 근사치를 반환합니다. (원자적 카운터 — 조회 시점에 따라 오차 가능) */
		[[nodiscard]] virtual std::size_t Available() const noexcept override { return available.load(std::memory_order_relaxed); }

	private:
		[[nodiscard]] static std::uint64_t Pack(const TaggedHead _head) noexcept { return (static_cast<std::uint64_t>(_head.tag) << 32) | _head.index; }
		[[nodiscard]] static TaggedHead Unpack(const std::uint64_t _packed) noexcept { return { static_cast<std::uint32_t>(_packed), static_cast<std::uint32_t>(_packed >> 32) }; }
	};

END_NS
