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

BEGIN_NS(ne::network)
	// 풀 기반 참조 카운팅 버퍼 블록.
	// Acquire → AddRef/Release 로 수명 관리.
	// Release 시 refCount == 0 이면 풀에 반납.
	class RawBuffer
	{
	private:
		RawBuffer(ne::byte_t* _data, std::size_t _capacity, ne::memory::IAllocator* _pool) noexcept;

	public:
		NEBULA_NON_COPYABLE_MOVABLE(RawBuffer)

	private:
		ne::byte_t* data{};
		std::size_t capacity{};
		ne::memory::IAllocator* pool{};
		std::atomic<int_t> refCount{ 1 };

	public:
		[[nodiscard]] static ne::Result<RawBuffer*, ne::OsError> Acquire(ne::memory::IAllocator& _pool, std::size_t _size, std::size_t _align = alignof(std::max_align_t)) noexcept;

	public:
		void AddRef() noexcept { refCount.fetch_add(1, std::memory_order_relaxed); }
		void Release() noexcept;

	public:
		[[nodiscard]] std::span<ne::byte_t> Data() noexcept { return { data, capacity }; }
		[[nodiscard]] std::span<const ne::byte_t> Data() const noexcept { return { data, capacity }; }
		[[nodiscard]] std::size_t Capacity() const noexcept { return capacity; }
	};

END_NS
