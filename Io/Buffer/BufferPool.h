//
// Created by hscloud on 26. 7. 10.
//

#pragma once
#include <cstddef>
#include <optional>
#include <vector>
#include "Base/Type.h"
#include "Io/IoResult.h"
#include "Io/Buffer/RegisteredBuffer.h"

BEGIN_NS(ne::io)
	class BufferPool;

	/**
	 * @class BufferPoolSlot
	 * @brief BufferPool 에서 대여한 고정 크기 버퍼 슬롯을 나타내는 RAII 핸들.
	 *
	 * BufferPool::Acquire() 로만 생성되며, 소멸 시 자신을 내준 BufferPool 에 자동으로 반환된다.
	 * 이동만 가능하고 복사할 수 없다. 대여 중인 슬롯이 있는 상태로 원본 BufferPool 을 이동/파괴하면 안 된다.
	 */
	class BufferPoolSlot
	{
		friend class BufferPool;

	private:
		BufferPoolSlot(BufferPool& _pool, const BufferView _view, const std::size_t _index) noexcept
			: pool(&_pool)
			, view(_view)
			, index(_index) {}

	public:
		~BufferPoolSlot();

		BufferPoolSlot(BufferPoolSlot&& _other) noexcept;
		BufferPoolSlot& operator=(BufferPoolSlot&& _other) noexcept;

		NEBULA_NON_COPYABLE(BufferPoolSlot)

	private:
		BufferPool* pool{ nullptr };
		BufferView view{};
		std::size_t index{ 0 };

	public:
		[[nodiscard]] const BufferView& View() const noexcept { return view; }
		[[nodiscard]] BufferHandle Handle() const noexcept;
	};

	/**
	 * @class BufferPool
	 * @brief 등록 버퍼(RegisteredBuffer) 위에 고정 크기 슬롯을 나눠 재사용하는 풀.
	 *
	 * 등록 연산은 비용이 크므로 slotSize * slotCount 바이트를 한 번에 할당·등록해 두고,
	 * Acquire()/Release() 로 고정 크기 슬롯을 빌리고 반환한다. 단일 io::Context(단일 스레드)에서
	 * 쓰는 것을 전제로 하며 내부 락이 없다 — 여러 스레드가 공유하려면 호출자가 동기화해야 한다.
	 */
	class BufferPool
	{
	private:
		BufferPool(std::vector<ne::byte_t>&& _storage, RegisteredBuffer&& _buffer, std::size_t _slotSize, std::size_t _slotCount) noexcept;

	public:
		~BufferPool() = default;

		NEBULA_NON_COPYABLE(BufferPool)
		NEBULA_DEFAULT_MOVE(BufferPool)

	private:
		std::vector<ne::byte_t> storage;
		RegisteredBuffer buffer;
		std::size_t slotSize{ 0 };
		std::size_t totalSlots{ 0 };
		std::vector<std::size_t> freeSlots;

	public:
		[[nodiscard]] static IoResult<BufferPool> Create(IEngine& _engine, std::size_t _slotSize, std::size_t _slotCount);

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
