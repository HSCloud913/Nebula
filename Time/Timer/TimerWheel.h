//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_set>
#include <vector>
#include "Base/Type.h"

BEGIN_NS(ne::time)
	// 최소 힙(min-heap) 기반 타이머. (구 hashed-wheel 대체)
	// 구 휠은 Tick 이 "경과 ms" 만큼 tick-by-tick 으로 catch-up 하여, 이벤트 루프가 오래 블록했다
	// 깨어나면 그 경과분(수백만 회)을 순회해 사실상 정지했다. 힙은 그 비용을 만료 개수로 한정한다.
	//   - Schedule     : O(log n)
	//   - Cancel       : O(1)          (지연 삭제 — id 를 live 에서 제거, 힙 엔트리는 만료 시 스킵)
	//   - Tick         : O(만료 · log n)  ← 경과 ms 와 무관
	//   - NextExpiryMs : O(1)          (힙 top peek)
	//
	// 시간 모델 : 모든 tick 값은 baseTime(생성 시각) 이후 경과한 실시간 ms 이다.
	// 운영에서는 steady_clock 을, 테스트에서는 페이크 클럭을 주입해 결정론적으로 시간을 제어한다.
	class TimerWheel
	{
	public:
		// 주입 가능한 클럭 seam — 호출 시점의 시각을 반환한다.
		using Clock = std::function<std::chrono::steady_clock::time_point()>;

	public:
		// 운영용 : steady_clock 기반 실시간 앵커링.
		TimerWheel();
		// 테스트용 : 클럭 주입. Tick / Schedule / NextExpiryMs 가 이 클럭의 경과 시간을 따른다.
		explicit TimerWheel(Clock _clock);
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
		std::vector<TimerEntry> heap;         // 이진 힙(수동 push/pop_heap — callback 를 이동해 꺼내기 위함)
		std::unordered_set<ulonglong_t> live; // 아직 발화/취소되지 않은 id. Cancel/Tick 에서 제거.
		std::atomic<ulonglong_t> nextId{ 1 };
		mutable std::mutex mutex;

	public:
		[[nodiscard]] ulonglong_t Schedule(std::chrono::milliseconds _delay, std::function<void_t()> _callback);
		bool_t Cancel(ulonglong_t _id);
		void_t Tick();

		// 현재 시각 기준 가장 빠른 타이머의 만료까지 남은 ms. 예약된 타이머가 없으면 -1(무기한 대기 의미).
		[[nodiscard]] int_t NextExpiryMs() const noexcept;

	private:
		// baseTime 이후 경과한 실시간 ms (음수는 0 으로 클램프).
		[[nodiscard]] ulonglong_t ElapsedMs() const noexcept;
	};

END_NS
