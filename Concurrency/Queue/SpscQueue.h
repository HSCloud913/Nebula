//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <atomic>
#include <cassert>
#include <cstddef>
#include <vector>
#include "Base/Type.h"

BEGIN_NS(ne::concurrency)
	/**
	 * @class SpscQueue
	 * @brief 단일 생산자·단일 소비자(SPSC) 전용 lock-free 링버퍼입니다.
	 *
	 * 생산자 스레드는 Enqueue만, 소비자 스레드는 Dequeue만 호출해야 안전합니다.
	 *
	 * @tparam T 큐에 저장할 값 타입.
	 * @note capacity는 2의 거듭제곱이어야 합니다(마스크 연산으로 인덱스를 감쌈).
	 */
	template <typename T>
	class SpscQueue
	{
	public:
		explicit SpscQueue(std::size_t _capacity)
			: capacity(_capacity)
			, mask(_capacity - 1)
			, buffer(_capacity) { assert((_capacity & (_capacity - 1)) == 0 && "SpscQueue: capacity must be power of 2"); }

		NEBULA_NON_COPYABLE_MOVABLE(SpscQueue)

	private:
		std::size_t capacity;
		std::size_t mask;
		std::vector<T> buffer;
		alignas(64) std::atomic<std::size_t> writePos{ 0 };
		alignas(64) std::atomic<std::size_t> readPos{ 0 };

	public:
		[[nodiscard]] bool_t Enqueue(T _value) noexcept
		{
			const std::size_t pos = writePos.load(std::memory_order_relaxed);
			const std::size_t next = (pos + 1) & mask;
			if (next == readPos.load(std::memory_order_acquire)) return false;

			buffer[pos] = std::move(_value);
			writePos.store(next, std::memory_order_release);

			return true;
		}

		[[nodiscard]] bool_t Dequeue(T& _out) noexcept
		{
			const std::size_t pos = readPos.load(std::memory_order_relaxed);
			if (pos == writePos.load(std::memory_order_acquire)) return false;

			_out = std::move(buffer[pos]);
			readPos.store((pos + 1) & mask, std::memory_order_release);

			return true;
		}

		[[nodiscard]] bool_t IsEmpty() const noexcept { return readPos.load(std::memory_order_acquire) == writePos.load(std::memory_order_acquire); }

		[[nodiscard]] bool_t IsFull() const noexcept
		{
			const std::size_t w = writePos.load(std::memory_order_acquire);
			const std::size_t next = (w + 1) & mask;
			return next == readPos.load(std::memory_order_acquire);
		}
	};

END_NS
