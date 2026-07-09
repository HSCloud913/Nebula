//
// Created by hscloud on 26. 7. 10.
//
// Level 3.5 — 등록 버퍼 풀. 등록(RegisterBuffer)은 비싼 연산이므로 slotSize*slotCount 바이트를
// 한 번에 할당·등록해두고, Acquire/Release 로 고정 크기 슬롯을 재사용한다.
// 단일 io::Context(단일 스레드) 에서 쓰는 것을 전제로 하며 내부 락이 없다 — 여러 스레드가 공유하려면
// 호출자가 동기화해야 한다.
//
// 수명 주의: BufferPoolSlot 은 자신을 내준 BufferPool 의 주소를 들고 있다 — 대여 중인 슬롯이 있는
// 상태로 BufferPool 을 이동/파괴하면 안 된다(RegisteredBuffer 의 "호출자가 수명 보장" 계약과 동일한
// 종류의 제약).

#pragma once
#include <cstddef>
#include <optional>
#include <vector>
#include "Base/Type.h"
#include "Io/IoResult.h"
#include "Io/Buffer/RegisteredBuffer.h"

BEGIN_NS(ne::io)
	class BufferPool;

	// BufferPool::Acquire() 가 돌려주는 슬롯 — 소멸 시 자동으로 풀에 반환된다(RAII).
	class BufferPoolSlot
	{
	private:
		friend class BufferPool;
		BufferPoolSlot(BufferPool& _pool, const std::size_t _index, const BufferView _view) noexcept
			: pool(&_pool)
			, index(_index)
			, view(_view) {}

	public:
		~BufferPoolSlot();

		NEBULA_NON_COPYABLE(BufferPoolSlot)

		BufferPoolSlot(BufferPoolSlot&& _other) noexcept;
		BufferPoolSlot& operator=(BufferPoolSlot&& _other) noexcept;

	private:
		BufferPool* pool{ nullptr };
		std::size_t index{ 0 };
		BufferView view{};

	public:
		[[nodiscard]] const BufferView& View() const noexcept { return view; }
		[[nodiscard]] BufferHandle Handle() const noexcept;
	};

	class BufferPool
	{
	private:
		BufferPool(std::vector<ne::byte_t>&& _storage, RegisteredBuffer&& _buffer, std::size_t _slotSize, std::size_t _slotCount) noexcept;

	public:
		~BufferPool() = default;

		NEBULA_NON_COPYABLE(BufferPool)
		NEBULA_DEFAULT_MOVE(BufferPool)

	private:
		std::vector<ne::byte_t> storage;    // 등록된 region 의 실제 소유자 — buffer(등록/해제) 보다 먼저 살아있어야 한다
		RegisteredBuffer buffer;            // storage 전체를 한 번에 등록(선언 순서상 storage 보다 나중에 소멸 = 먼저 해제)
		std::size_t slotSize{ 0 };
		std::size_t totalSlots{ 0 };
		std::vector<std::size_t> freeSlots; // 대여 가능한 슬롯 인덱스 스택

	public:
		// _engine 이 등록 버퍼를 지원하지 않으면 UNSUPPORTED. _slotSize * _slotCount 바이트를 할당·등록한다.
		[[nodiscard]] static IoResult<BufferPool> Create(IEngine& _engine, std::size_t _slotSize, std::size_t _slotCount);

		// 빈 슬롯이 없으면 std::nullopt.
		[[nodiscard]] std::optional<BufferPoolSlot> Acquire() noexcept;

	private:
		friend class BufferPoolSlot;
		void_t Release(const std::size_t _index) noexcept { freeSlots.push_back(_index); }

	public:
		[[nodiscard]] std::size_t SlotSize() const noexcept { return slotSize; }
		[[nodiscard]] std::size_t Capacity() const noexcept { return totalSlots; }
		[[nodiscard]] std::size_t Available() const noexcept { return freeSlots.size(); }
	};

END_NS