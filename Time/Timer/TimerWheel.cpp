//
// Created by hscloud on 26. 6. 30.
//

#include "TimerWheel.h"

#include <algorithm>
#include <limits>



BEGIN_NS(ne::time)
	ulonglong_t TimerWheel::Schedule(const std::chrono::milliseconds _delay, std::function<void()> _callback)
	{
		const ulonglong_t id = nextId.fetch_add(1, std::memory_order_relaxed);
		const ulonglong_t tick = currentTick.load(std::memory_order_relaxed) + static_cast<ulonglong_t>(_delay.count());
		const std::size_t bucket = tick & BucketMask;

		std::lock_guard lock(mutex);
		buckets[bucket].push_back({ id, tick, std::move(_callback) });

		return id;
	}

	bool_t TimerWheel::Cancel(const ulonglong_t _id)
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
		const ulonglong_t tick = currentTick.fetch_add(1, std::memory_order_relaxed) + 1;
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

	int_t TimerWheel::NextExpiryMs() const noexcept
	{
		std::lock_guard lock(mutex);
		const ulonglong_t current = currentTick.load(std::memory_order_relaxed);

		ulonglong_t earliest = std::numeric_limits<ulonglong_t>::max();
		for (const auto& bucket : buckets)
		{
			for (const auto& entry : bucket)
			{
				if (entry.expireTick < earliest)
					earliest = entry.expireTick;
			}
		}

		if (earliest == std::numeric_limits<ulonglong_t>::max()) return -1;

		if (earliest <= current) return 0;

		const ulonglong_t delta = earliest - current;
		if (delta > static_cast<ulonglong_t>(std::numeric_limits<int_t>::max())) return std::numeric_limits<int_t>::max();

		return static_cast<int_t>(delta);
	}
END_NS
