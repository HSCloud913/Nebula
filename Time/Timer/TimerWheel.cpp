//
// Created by hscloud on 26. 6. 30.
//

#include "Time/Timer/TimerWheel.h"

#include <algorithm>
#include <limits>



BEGIN_NS(ne::time)
	// 지연 삭제(lazy-deletion) 방식의 min-heap 기반 타이머 설계:
	// heap은 만료 시각(expireTick) 기준 최소 힙으로 "가장 빨리 만료될 타이머가 무엇인지"만 O(log n)에 관리하고,
	// live는 "아직 취소되지 않은 타이머 id" 집합이다. Cancel은 힙을 건드리지 않고 live에서만 제거하므로 O(1)이며,
	// 힙에 남은 취소된 엔트리는 Tick에서 팝될 때 live에 없음을 확인하고 그냥 버려진다(스킵).
	// 힙 재구성(compaction)은 dead 엔트리 비율이 일정 이상일 때만 수행해 메모리 증가를 억제한다.
	ulonglong_t TimerWheel::Schedule(const std::chrono::milliseconds _delay, std::function<void_t()> _callback)
	{
		const ulonglong_t id = nextId.fetch_add(1, std::memory_order_relaxed);
		const ulonglong_t delay = _delay.count() > 0 ? static_cast<ulonglong_t>(_delay.count()) : 0;
		const ulonglong_t tick = ElapsedMs() + delay;

		std::lock_guard lock(mutex);
		heap.push_back({ id, tick, std::move(_callback) });
		std::ranges::push_heap(heap, LaterExpiry{});
		live.insert(id);

		return id;
	}

	// 지연 삭제: live 에서만 제거한다. 힙 엔트리는 만료 시 Tick 이 live 에 없음을 보고 스킵·폐기.
	bool_t TimerWheel::Cancel(const ulonglong_t _id)
	{
		std::lock_guard lock(mutex);
		return live.erase(_id) > 0;
	}

	void_t TimerWheel::Tick()
	{
		const ulonglong_t target = ElapsedMs();

		std::vector<TimerEntry> fired;
		{
			std::lock_guard lock(mutex);

			while (!heap.empty() && heap.front().expireTick <= target)
			{
				std::ranges::pop_heap(heap, LaterExpiry{}); // top → back
				TimerEntry entry = std::move(heap.back());
				heap.pop_back();

				if (live.erase(entry.id) > 0) fired.push_back(std::move(entry)); // 살아있으면 발화 예약
				// else: 이미 취소됨 — 스킵(폐기). live 를 늘리지 않으므로 세트는 항상 유효 타이머만 유지.
			}

			// 취소로 남은 dead 엔트리(만료 전이라 아직 힙에 있음)가 과반이면 힙을 재구성해 메모리 증가를 막는다.
			if (heap.size() > 64 && heap.size() > live.size() * 2)
			{
				std::vector<TimerEntry> alive;
				alive.reserve(live.size());
				for (auto& entry : heap)
					if (live.contains(entry.id)) alive.push_back(std::move(entry));

				heap = std::move(alive);
				std::ranges::make_heap(heap, LaterExpiry{});
			}
		}

		for (auto& entry : fired) entry.callback();
	}


	int_t TimerWheel::NextExpiryMs() const noexcept
	{
		std::lock_guard lock(mutex);
		if (heap.empty()) return -1;

		// top 이 취소된 엔트리면 실제보다 이른 값을 돌려줄 수 있으나(→ 스퓨리어스 웨이크 1회), 그 즉시
		// Tick 이 해당 엔트리를 팝·폐기하므로 안전하다.
		const ulonglong_t earliest = heap.front().expireTick;
		const ulonglong_t now = ElapsedMs();
		if (earliest <= now) return 0;

		const ulonglong_t delta = earliest - now;
		if (delta > static_cast<ulonglong_t>(std::numeric_limits<int_t>::max())) return std::numeric_limits<int_t>::max();

		return static_cast<int_t>(delta);
	}



	ulonglong_t TimerWheel::ElapsedMs() const noexcept
	{
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock() - baseTime).count();
		return ms > 0 ? static_cast<ulonglong_t>(ms) : 0;
	}

END_NS
