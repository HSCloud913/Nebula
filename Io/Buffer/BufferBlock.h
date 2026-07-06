//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <atomic>
#include <cstddef>
#include <span>
#include "Allocator/PoolAllocator.h"
#include "Result.h"
#include "Error.h"
#include "Type.h"

BEGIN_NS(ne::io)
	// 풀 기반 참조 카운팅 버퍼 블록.
	// Acquire → AddRef/Release 로 수명 관리.
	// Release 시 refCount == 0 이면 풀에 반납.
	class BufferBlock
	{
	private:
		BufferBlock(ne::byte_t* _data, const std::size_t _capacity, ne::memory::IAllocator* _pool) noexcept
			: data(_data)
			, capacity(_capacity)
			, pool(_pool) {}

	public:
		NEBULA_NON_COPYABLE_MOVABLE(BufferBlock)

	private:
		ne::byte_t* data{};
		std::size_t capacity{};
		ne::memory::IAllocator* pool{};
		std::atomic<int_t> refCount{ 1 };

	public:
		[[nodiscard]] static ne::Result<BufferBlock*, ne::OsError> Acquire(ne::memory::IAllocator& _pool, const std::size_t _size, const std::size_t _align = alignof(std::max_align_t)) noexcept
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

	public:
		void AddRef() noexcept { refCount.fetch_add(1, std::memory_order_relaxed); }
		void Release() noexcept
		{
			if (refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
			{
				auto* tempPool = pool;
				const std::size_t size = capacity + sizeof(BufferBlock);

				this->~BufferBlock();

				tempPool->Deallocate(this, size);
			}
		}

	public:
		[[nodiscard]] std::span<ne::byte_t> Data() noexcept { return { data, capacity }; }
		[[nodiscard]] std::span<const ne::byte_t> Data() const noexcept { return { data, capacity }; }
		[[nodiscard]] std::size_t Capacity() const noexcept { return capacity; }
	};

END_NS
