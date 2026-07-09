//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstddef>
#include <mutex>
#include "Memory/Allocator/IAllocator.h"
#include "Base/Type.h"

BEGIN_NS(ne::memory)
	// 고정 크기 블록 풀. free list 기반.
	// 스레드 안전 (mutex 보호, 추후 lock-free 교체 예정).
	// 모든 블록은 생성 시 지정한 _alignment(2의 거듭제곱)로 정렬된다 — io_uring fixed buffer /
	// Windows unbuffered I/O 의 섹터 정렬(512·4096) 요구를 만족시키기 위함.
	class PoolAllocator final :public IAllocator
	{
	public:
		explicit PoolAllocator(std::size_t _blockSize, std::size_t _blockCount, std::size_t _alignment = alignof(std::max_align_t));
		virtual ~PoolAllocator() override;

		NEBULA_NON_COPYABLE_MOVABLE(PoolAllocator)

	private:
		struct FreeNode
		{
			FreeNode* next;
		};

	private:
		// 선언 순서 주의: alignment 가 blockSize 초기화에 쓰이므로 먼저 선언한다(멤버 초기화 순서 = 선언 순서).
		std::size_t alignment;
		std::size_t blockSize;
		std::size_t blockCount;
		std::size_t available;
		ne::byte_t* pool{ nullptr };
		FreeNode* freeList{ nullptr };
		mutable std::mutex mutex;

	public:
		[[nodiscard]] virtual void_t* Allocate(std::size_t _size, std::size_t _align) override;
		virtual void_t Deallocate(void_t* _ptr, std::size_t _size) noexcept override;
		[[nodiscard]] virtual std::size_t Available() const noexcept override;
	};

END_NS
