//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>
#include "Type.h"

BEGIN_NS(ne::time)
	// Hashed Wheel Timer.
	// - 버킷 수 : 512 (2의 거듭제곱)
	// - tick 단위 : 1 ms
	// - Schedule / Cancel : O(1) 상각
	// - Tick() : 이벤트 루프가 wakeup 마다 호출 — 주입된 클럭의 실제 경과 ms 까지 catch-up.
	//
	// 시간 모델 : 모든 tick 값은 baseTime(생성 시각) 이후 경과한 "실시간 ms" 이다.
	// 운영에서는 steady_clock 을, 테스트에서는 페이크 클럭을 주입해 결정론적으로 시간을 제어한다.
	class TimerWheel
	{
	public:
		// 주입 가능한 클럭 seam — 호출 시점의 시각을 반환한다.
		using Clock = std::function<std::chrono::steady_clock::time_point()>;

		// 운영용 : steady_clock 기반 실시간 앵커링.
		TimerWheel();
		// 테스트용 : 클럭 주입. Tick / Schedule / NextExpiryMs 가 이 클럭의 경과 시간을 따른다.
		explicit TimerWheel(Clock _clock);
		~TimerWheel() = default;

		NEBULA_NON_COPYABLE_MOVABLE(TimerWheel)

	private:
		static constexpr std::size_t BucketCount = 512;
		static constexpr std::size_t BucketMask = BucketCount - 1;

	private:
		struct TimerEntry
		{
			ulonglong_t id;
			ulonglong_t expireTick;
			std::function<void()> cb;
		};

		Clock clock;
		std::chrono::steady_clock::time_point baseTime;

		std::array<std::vector<TimerEntry>, BucketCount> buckets;
		std::atomic<ulonglong_t> nextId{ 1 };
		std::atomic<ulonglong_t> currentTick{ 0 };
		mutable std::mutex mutex;

		// baseTime 이후 경과한 실시간 ms (음수는 0 으로 클램프).
		[[nodiscard]] ulonglong_t ElapsedMs() const noexcept;

	public:
		[[nodiscard]] ulonglong_t Schedule(std::chrono::milliseconds _delay, std::function<void()> _callback);
		bool_t Cancel(ulonglong_t _id);
		void_t Tick();

		// 현재 시각 기준 가장 빠른 타이머의 만료까지 남은 ms를 반환.
		// 예약된 타이머가 없으면 -1 반환 (무기한 대기 의미).
		[[nodiscard]] int_t NextExpiryMs() const noexcept;
	};

END_NS
