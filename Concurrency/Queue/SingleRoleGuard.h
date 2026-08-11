//
// Created by hscloud on 26. 7. 23.
//

#pragma once
#include "Base/Type.h"

#ifndef NDEBUG
#   include <atomic>
#   include <cassert>
#endif

namespace ne::concurrency
{
#ifndef NDEBUG
	/**
	 * @class SingleRoleGuard
	 * @brief (디버그 전용) 한 역할(단일 생산자/단일 소비자)이 두 스레드에서 "동시에" 실행되는지 감지한다.
	 *
	 * Enter()가 돌려주는 스코프 객체가 살아있는 동안 역할을 점유하며, 그 사이 다른 스레드가 같은
	 * 역할로 진입하면 assert 로 잡는다. join 등으로 동기화된 뒤 다른 스레드가 이어받는 "순차 인계"는
	 * 구간이 겹치지 않으므로 허용된다. 릴리스(NDEBUG)에서는 빈 타입 + no-op 으로 완전히 사라진다.
	 */
	class SingleRoleGuard
	{
	public:
		class Scope
		{
		public:
			Scope(std::atomic<bool_t>& _active, const char* _role) noexcept
				: active(_active)
			{
				const bool_t wasActive = active.exchange(true, std::memory_order_acquire);
				assert(!wasActive && _role);
				(void_t)_role;
			}

			~Scope() { active.store(false, std::memory_order_release); }

			NEBULA_NON_COPYABLE_MOVABLE(Scope)

		private:
			std::atomic<bool_t>& active;
		};

		[[nodiscard]] Scope Enter(const char* _role) noexcept { return Scope{ active, _role }; }

	private:
		std::atomic<bool_t> active{ false };
	};
#else
	class SingleRoleGuard
	{
	public:
		struct Scope {};

		[[nodiscard]] Scope Enter(const char*) noexcept { return {}; }
	};
#endif
}
