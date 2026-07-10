//
// Created by hscloud on 26. 7. 10.
//

#include "Io/Buffer/BufferPool.h"

#include <utility>

BEGIN_NS(ne::io)
	BufferPoolSlot::~BufferPoolSlot() { if (pool != nullptr) pool->Release(index); }

	BufferPoolSlot::BufferPoolSlot(BufferPoolSlot&& _other) noexcept
		: pool(_other.pool)
		, index(_other.index)
		, view(_other.view) { _other.pool = nullptr; }

	BufferPoolSlot& BufferPoolSlot::operator=(BufferPoolSlot&& _other) noexcept
	{
		if (this != &_other)
		{
			if (pool != nullptr) pool->Release(index);

			pool = _other.pool;
			index = _other.index;
			view = _other.view;
			_other.pool = nullptr;
		}

		return *this;
	}



	BufferHandle BufferPoolSlot::Handle() const noexcept { return pool != nullptr ? pool->buffer.Handle() : BufferHandle{}; }



	/*--------------------------------------------------*/



	BufferPool::BufferPool(std::vector<ne::byte_t>&& _storage, RegisteredBuffer&& _buffer, const std::size_t _slotSize, const std::size_t _slotCount) noexcept
		: storage(std::move(_storage))
		, buffer(std::move(_buffer))
		, slotSize(_slotSize)
		, totalSlots(_slotCount)
	{
		freeSlots.reserve(_slotCount);
		for (std::size_t i = _slotCount; i-- > 0;) freeSlots.push_back(i);
	}



	IoResult<BufferPool> BufferPool::Create(IEngine& _engine, const std::size_t _slotSize, const std::size_t _slotCount)
	{
		if (_slotSize == 0 || _slotCount == 0) return IoResult<BufferPool>::Error(IoError{ IoErrorKind::INVALID_BUFFER, "slotSize/slotCount must be non-zero" });

		std::vector<ne::byte_t> storage(_slotSize * _slotCount);
		const std::span<ne::byte_t> region{ storage.data(), storage.size() };

		auto registered = RegisteredBuffer::Register(_engine, region);
		if (registered.IsError()) return IoResult<BufferPool>::Error(std::move(registered.Error()));

		return IoResult<BufferPool>::Ok(BufferPool{ std::move(storage), std::move(registered.Value()), _slotSize, _slotCount });
	}

	std::optional<BufferPoolSlot> BufferPool::Acquire() noexcept
	{
		if (freeSlots.empty()) return std::nullopt;

		const std::size_t index = freeSlots.back();
		freeSlots.pop_back();

		return BufferPoolSlot{ *this, index, buffer.View(index * slotSize, slotSize) };
	}

END_NS
