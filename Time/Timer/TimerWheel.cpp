//
// Created by hscloud on 26. 6. 30.
//

#include "TimerWheel.h"

#include <algorithm>
#include <limits>



BEGIN_NS(ne::time)
	TimerWheel::TimerWheel()
		: TimerWheel([] { return std::chrono::steady_clock::now(); })
	{
	}

	TimerWheel::TimerWheel(Clock _clock)
		: clock(std::move(_clock))
		, baseTime(clock())
	{
	}

	ulonglong_t TimerWheel::ElapsedMs() const noexcept
	{
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock() - baseTime).count();
		return ms > 0 ? static_cast<ulonglong_t>(ms) : 0;
	}

	ulonglong_t TimerWheel::Schedule(const std::chrono::milliseconds _delay, std::function<void()> _callback)
	{
		const ulonglong_t id = nextId.fetch_add(1, std::memory_order_relaxed);
		const ulonglong_t delay = _delay.count() > 0 ? static_cast<ulonglong_t>(_delay.count()) : 0;
		const ulonglong_t tick = ElapsedMs() + delay;
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
		// 실제 경과 시간까지 currentTick 을 따라잡는다. wakeup 1 회가 여러 ms 를 블록했을 수
		// 있으므로(GQCS/epoll_wait 이 NextExpiryMs 만큼 대기) 그 사이 만료된 타이머를 모두
		// 발화하려면 지나온 tick 마다 해당 버킷을 훑어야 한다.
		const ulonglong_t target = ElapsedMs();

		std::vector<TimerEntry> fired;
		{
			std::lock_guard lock(mutex);

			ulonglong_t tick = currentTick.load(std::memory_order_relaxed);
			while (tick < target)
			{
				++tick;
				auto& vec = buckets[tick & BucketMask];
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

			currentTick.store(tick, std::memory_order_relaxed);
		}

		for (auto& entry : fired) entry.cb();
	}

	int_t TimerWheel::NextExpiryMs() const noexcept
	{
		std::lock_guard lock(mutex);

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

		// currentTick(마지막 Tick 시점) 이 아니라 실제 현재 시각 기준으로 남은 시간을 계산한다.
		// 그래야 이벤트 루프가 다음 만료까지 정확히 블록한다.
		const ulonglong_t now = ElapsedMs();
		if (earliest <= now) return 0;

		const ulonglong_t delta = earliest - now;
		if (delta > static_cast<ulonglong_t>(std::numeric_limits<int_t>::max())) return std::numeric_limits<int_t>::max();

		return static_cast<int_t>(delta);
	}
END_NS
