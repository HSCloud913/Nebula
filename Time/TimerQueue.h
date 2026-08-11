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

namespace ne::time
{
	/**
	 * @class TimerQueue
	 * @brief 최소 힙(min-heap) 기반 타이머 큐입니다.
	 *
	 * 만료 시각(expireTick) 기준 최소 힙으로 타이머를 관리해, 비용을 경과 시간이 아닌
	 * 만료 개수에 비례하게 한정합니다.
	 *
	 * @note 예전 이름은 TimerWheel 이었지만, timer wheel(hashed hierarchical timing wheel)은 삽입이
	 * O(1) 인 버킷 배열 구조를 가리키는 별개의 알고리즘입니다. 이 구현은 이진 힙이라 삽입이 O(log n)
	 * 이고, 이름이 복잡도를 잘못 알려 주고 있었습니다(클래스 설명이 자기 이름을 부정해야 하는 상태).
	 *   - Schedule     : O(log n)
	 *   - Cancel       : O(1)          (지연 삭제 — id를 live에서 제거, 힙 엔트리는 만료 시 스킵)
	 *   - Tick         : O(만료 · log n)  ← 경과 ms와 무관
	 *   - NextExpiryMs : O(1)          (힙 top peek)
	 *
	 * @note 시간 모델: 모든 tick 값은 baseTime(생성 시각) 이후 경과한 실시간 ms입니다.
	 * 운영에서는 steady_clock을, 테스트에서는 페이크 클럭을 주입해 결정론적으로 시간을 제어합니다.
	 */
	class TimerQueue
	{
	public:
		/** @brief 주입 가능한 클럭 seam — 호출 시점의 시각을 반환합니다. */
		using Clock = std::function<std::chrono::steady_clock::time_point()>;

	public:
		/** @brief 운영용: steady_clock 기반 실시간 앵커링. */
		TimerQueue()
			: TimerQueue([] { return std::chrono::steady_clock::now(); }) {}

		/** @brief 테스트용: 클럭 주입. Tick/Schedule/NextExpiryMs가 이 클럭의 경과 시간을 따릅니다. */
		explicit TimerQueue(Clock _clock)
			: clock(std::move(_clock))
			, baseTime(clock()) {}

		~TimerQueue() = default;

		NEBULA_NON_COPYABLE_MOVABLE(TimerQueue)

	private:
		struct TimerEntry
		{
			ulonglong_t id;
			ulonglong_t expireTick;
			std::function<void_t()> callback;
			ulonglong_t periodMs{ 0 }; // 0 이면 1회성, 그 외에는 이 주기로 재예약
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

		/**
		 * @brief _period 마다 _callback 을 반복 실행하도록 예약합니다. Cancel(id) 로만 멈춥니다.
		 *
		 * 유휴 연결 스윕, HTTP/2 PING keepalive, 캐시 TTL 만료처럼 "끝이 없는 주기 작업" 을 매번
		 * 재예약하지 않아도 되게 합니다.
		 *
		 * @note 다음 만료는 **콜백 실행 시각 기준**이 아니라 예정 만료 시각 기준으로 누적합니다. 콜백이
		 * 오래 걸려 여러 주기를 넘겼다면 밀린 만큼을 몰아 실행하지 않고, 현재 시각 이후의 첫 경계로
		 * 건너뜁니다(타이머 폭주 방지).
		 * @note _period 가 0 이하면 예약하지 않고 0 을 반환합니다(무한 루프 방지).
		 */
		[[nodiscard]] ulonglong_t ScheduleRepeating(std::chrono::milliseconds _period, std::function<void_t()> _callback);

		/** @brief 예약된 타이머를 취소합니다. 이미 발화됐거나 존재하지 않는 id 면 false 를 반환합니다. */
		bool_t Cancel(ulonglong_t _id);

		/** @brief 현재 시각까지 만료된 타이머들의 콜백을 실행합니다. 이벤트 루프에서 주기적으로 호출해야 합니다. */
		void_t Tick();

		/** @brief 현재 시각 기준 가장 빠른 타이머의 만료까지 남은 ms. 예약된 타이머가 없으면 -1. (무기한 대기 의미) */
		[[nodiscard]] int_t NextExpiryMs() const noexcept;

		/**
		 * @brief 이 큐가 쓰는 시계의 현재 시각입니다.
		 *
		 * 데드라인을 상대 지연으로 바꾸는 계산은 반드시 이 시각을 기준으로 해야 합니다 —
		 * steady_clock::now() 를 직접 쓰면 주입된 페이크 클럭과 기준이 어긋나 엉뚱한 지연이 나옵니다
		 * (과거 Deadline() 이 그 상태였습니다).
		 */
		[[nodiscard]] std::chrono::steady_clock::time_point Now() const { return clock(); }

	private:
		/** @brief baseTime 이후 경과한 실시간 ms. (음수는 0 으로 클램프) */
		[[nodiscard]] ulonglong_t ElapsedMs() const noexcept;

		/** @brief Schedule/ScheduleRepeating 공용 등록 경로. _periodMs 가 0 이 아니면 반복 타이머다. */
		[[nodiscard]] ulonglong_t ScheduleAt(std::chrono::milliseconds _delay, ulonglong_t _periodMs, std::function<void_t()> _callback);
	};
}
