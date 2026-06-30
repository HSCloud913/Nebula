//
// Created by hscloud on 26. 6. 30.
//

#include "TimerWheel.h"
#include <algorithm>



BEGIN_NS(ne::time)
	uint64_t TimerWheel::Schedule(const Duration _delay, std::function<void()> _callback)
	{
		const uint64_t id = nextId.fetch_add(1, std::memory_order_relaxed);
		const uint64_t tick = currentTick.load(std::memory_order_relaxed) + static_cast<uint64_t>(_delay.count());
		const std::size_t bucket = tick & BucketMask;

		std::lock_guard lock(mutex);
		buckets[bucket].push_back({ id, tick, std::move(_callback) });

		return id;
	}

	bool_t TimerWheel::Cancel(const uint64_t _id)
	{
		std::lock_guard<std::mutex> lock(mutex);

		for (auto& bucket : buckets)
		{
			auto iter = std::find_if(bucket.begin(), bucket.end(), [_id](const TimerEntry& e) { return e.id == _id; });
			if (iter != bucket.end())
			{
				bucket.erase(iter);
				return true;
			}
		}

		return false;
	}

	void_t TimerWheel::Tick()
	{
		const uint64_t tick = currentTick.fetch_add(1, std::memory_order_relaxed) + 1;
		const std::size_t bucket = tick & BucketMask;

		std::vector<TimerEntry> fired;
		{
			std::lock_guard lock(mutex);

			auto& vec = buckets[bucket];
			auto iter = vec.begin();
			while (iter != vec.end())
			{
				if (iter->expireTick <= tick)
				{
					fired.push_back(std::move(*iter));
					iter = vec.erase(iter);
				}
				else ++iter;
			}
		}

		for (auto& entry : fired) entry.cb();
	}
END_NS
