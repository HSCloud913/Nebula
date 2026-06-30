//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>
#include "Clock.h"
#include "Type.h"

BEGIN_NS(ne::time)
	// Hashed Wheel Timer.
	// - 버킷 수 : 512 (2의 거듭제곱)
	// - tick 단위 : 1 ms
	// - Schedule / Cancel : O(1) 상각
	// - Tick() : 이벤트 루프에서 매 ms 호출
	class TimerWheel
	{
	public:
		TimerWheel() noexcept = default;
		~TimerWheel() = default;
		NEBULA_NON_COPYABLE_MOVABLE(TimerWheel)

	private:
		static constexpr std::size_t BucketCount = 512;
		static constexpr std::size_t BucketMask = BucketCount - 1;

	private:
		struct TimerEntry
		{
			uint64_t id;
			uint64_t expireTick;
			std::function<void()> cb;
		};

		std::array<std::vector<TimerEntry>, BucketCount> buckets;
		std::atomic<uint64_t> nextId{ 1 };
		std::atomic<uint64_t> currentTick{ 0 };
		mutable std::mutex mutex;

	public:
		[[nodiscard]] uint64_t Schedule(Duration _delay, std::function<void()> _callback);
		bool_t Cancel(uint64_t _id);
		void_t Tick();
	};

END_NS
