//
// Created by hscloud on 26. 8. 12.
//

#pragma once
#include <chrono>
#include "Base/Type.h"

namespace ne::time
{
	/**
	 * @class Deadline
	 * @brief "언제까지" 를 나타내는 절대 시각 값 타입입니다.
	 *
	 * 상대 지연(duration)만으로 시한을 다루면 계층을 내려갈 때마다 남은 시간을 다시 계산해야 하고,
	 * 그 과정에서 각 단계가 자기 몫의 시간을 새로 부여받아 전체 예산이 단계 수만큼 늘어납니다
	 * (예: 헤더 15s + 본문 30s 를 각각 주면 한 요청이 45s 를 쓸 수 있다). 절대 시각을 넘기면
	 * 여러 단계가 **하나의 예산**을 공유하므로 그런 누적이 생기지 않습니다.
	 *
	 * @note steady_clock 기준입니다 — 시스템 시계 변경(NTP 보정, 사용자 조작)에 영향받지 않아야
	 * 시한이 의미를 갖기 때문입니다.
	 * @note 기본 생성 상태는 "무기한"(시한 없음)입니다. 0 이 아니라 무기한을 기본으로 둔 이유는,
	 * 초기화를 잊었을 때 즉시 만료돼 요청이 통째로 실패하는 쪽이 훨씬 찾기 어려운 버그이기 때문입니다.
	 */
	class Deadline
	{
	public:
		using Clock = std::chrono::steady_clock;

	public:
		/** @brief 무기한 데드라인(시한 없음). */
		Deadline() = default;

		/** @brief 절대 시각으로 만듭니다. */
		explicit Deadline(const Clock::time_point _expiry) noexcept
			: expiry(_expiry)
			, hasExpiry(true) {}

	private:
		Clock::time_point expiry{};
		bool_t hasExpiry{ false };

	public:
		/**
		 * @brief 지금(_now)으로부터 _duration 뒤가 시한인 Deadline 을 만듭니다.
		 * @param _now 기준 시각. TimerQueue::Now() 를 넘기세요 — steady_clock::now() 를 직접 쓰면
		 * 페이크 클럭을 주입한 테스트에서 기준이 어긋납니다.
		 * @note _duration 이 0 이하면 무기한으로 봅니다(설정하지 않은 것과 같게 취급).
		 */
		[[nodiscard]] static Deadline After(const Clock::time_point _now, const std::chrono::milliseconds _duration) noexcept
		{
			if (_duration.count() <= 0) return {};

			return Deadline{ _now + _duration };
		}

	public:
		/** @brief 시한이 설정되어 있는지(false 면 무기한). */
		[[nodiscard]] bool_t HasExpiry() const noexcept { return hasExpiry; }

		/** @brief _now 기준으로 이미 지났는지. 무기한이면 항상 false. */
		[[nodiscard]] bool_t IsExpired(const Clock::time_point _now) const noexcept { return hasExpiry && _now >= expiry; }

		/**
		 * @brief _now 기준 남은 시간. 이미 지났으면 0.
		 * @note 무기한일 때는 "매우 큰 값" 이 아니라 24시간을 돌려줍니다 — 타이머 큐에 사실상 무한한
		 * 지연을 넣으면 오버플로 위험이 있고, 무기한 데드라인은 애초에 타이머를 걸지 않는 것이 맞습니다
		 * (HasExpiry() 로 먼저 분기하세요). 이 값은 그 분기를 잊었을 때의 안전한 상한입니다.
		 */
		[[nodiscard]] std::chrono::milliseconds Remaining(const Clock::time_point _now) const noexcept
		{
			if (!hasExpiry) return std::chrono::hours(24);
			if (_now >= expiry) return std::chrono::milliseconds::zero();

			return std::chrono::duration_cast<std::chrono::milliseconds>(expiry - _now);
		}

		/** @brief 두 데드라인 중 더 이른 것(둘 다 무기한이면 무기한) — 상위 예산과 단계 예산을 합칠 때 씁니다. */
		[[nodiscard]] Deadline Earliest(const Deadline _other) const noexcept
		{
			if (!hasExpiry) return _other;
			if (!_other.hasExpiry) return *this;

			return Deadline{ expiry < _other.expiry ? expiry : _other.expiry };
		}
	};
}
