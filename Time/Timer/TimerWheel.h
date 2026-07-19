//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_set>
#include <vector>
#include "Base/Type.h"

BEGIN_NS(ne::time)
	/**
	 * @class TimerWheel
	 * @brief 최소 힙(min-heap) 기반 타이머입니다.
	 *
	 * 만료 시각(expireTick) 기준 최소 힙으로 타이머를 관리해, 비용을 경과 시간이 아닌
	 * 만료 개수에 비례하게 한정합니다.
	 *   - Schedule     : O(log n)
	 *   - Cancel       : O(1)          (지연 삭제 — id를 live에서 제거, 힙 엔트리는 만료 시 스킵)
	 *   - Tick         : O(만료 · log n)  ← 경과 ms와 무관
	 *   - NextExpiryMs : O(1)          (힙 top peek)
	 *
	 * @note 시간 모델: 모든 tick 값은 baseTime(생성 시각) 이후 경과한 실시간 ms입니다.
	 * 운영에서는 steady_clock을, 테스트에서는 페이크 클럭을 주입해 결정론적으로 시간을 제어합니다.
	 */
	class TimerWheel
	{
	public:
		/** @brief 주입 가능한 클럭 seam — 호출 시점의 시각을 반환합니다. */
		using Clock = std::function<std::chrono::steady_clock::time_point()>;

	public:
		/** @brief 운영용: steady_clock 기반 실시간 앵커링. */
		TimerWheel()
			: TimerWheel([] { return std::chrono::steady_clock::now(); }) {}

		/** @brief 테스트용: 클럭 주입. Tick/Schedule/NextExpiryMs가 이 클럭의 경과 시간을 따릅니다. */
		explicit TimerWheel(Clock _clock)
			: clock(std::move(_clock))
			, baseTime(clock()) {}

		~TimerWheel() = default;

		NEBULA_NON_COPYABLE_MOVABLE(TimerWheel)

	private:
		struct TimerEntry
		{
			ulonglong_t id;
			ulonglong_t expireTick;
			std::function<void_t()> callback;
		};

		// push_heap/pop_heap 는 기본 max-heap 이므로, 가장 이른 expireTick 을 top 에 두려면 '>' 로 비교한다.
		struct LaterExpiry
		{
			[[nodiscard]] bool_t operator()(const TimerEntry& _lhs, const TimerEntry& _rhs) const noexcept { return _lhs.expireTick > _rhs.expireTick; }
		};

	private:
		Clock clock;
		std::chrono::steady_clock::time_point baseTime;
		std::vector<TimerEntry> heap; // 이진 힙(수동 push/pop_heap — callback 를 이동해 꺼내기 위함)
		std::unordered_set<ulonglong_t> live; // 아직 발화/취소되지 않은 id. Cancel/Tick 에서 제거.
		std::atomic<ulonglong_t> nextId{ 1 };
		mutable std::mutex mutex;

	public:
		/** @brief _delay 후 _callback 을 실행하도록 예약하고, 취소/식별에 쓸 타이머 id 를 반환합니다. */
		[[nodiscard]] ulonglong_t Schedule(std::chrono::milliseconds _delay, std::function<void_t()> _callback);

		/** @brief 예약된 타이머를 취소합니다. 이미 발화됐거나 존재하지 않는 id 면 false 를 반환합니다. */
		bool_t Cancel(ulonglong_t _id);

		/** @brief 현재 시각까지 만료된 타이머들의 콜백을 실행합니다. 이벤트 루프에서 주기적으로 호출해야 합니다. */
		void_t Tick();

		/** @brief 현재 시각 기준 가장 빠른 타이머의 만료까지 남은 ms. 예약된 타이머가 없으면 -1. (무기한 대기 의미) */
		[[nodiscard]] int_t NextExpiryMs() const noexcept;

	private:
		/** @brief baseTime 이후 경과한 실시간 ms (음수는 0 으로 클램프). */
		[[nodiscard]] ulonglong_t ElapsedMs() const noexcept;
	};

END_NS
