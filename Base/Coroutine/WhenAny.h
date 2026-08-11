//
// Created by hscloud on 26. 7. 24.
//

#pragma once
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>
#include "Base/Coroutine/Task.h"
#include "Base/Coroutine/IExecutor.h"
#include "Base/Coroutine/Race.h"

namespace ne
{
	/**
	 * @class WhenAnyResult
	 * @brief WhenAny() 가 돌려주는, 가장 먼저 완료된 태스크의 인덱스와 그 값입니다.
	 *
	 * index 는 WhenAny 에 넘긴 벡터에서 승리한 태스크의 위치이고, value 는 그 태스크가
	 * co_return 한 값입니다.
	 */
	template <typename T>
	struct WhenAnyResult
	{
		std::size_t index;
		T value;
	};

	namespace internal
	{
		template <typename T>
		ne::Task<void_t> WhenAnyRacer(IExecutor& _executor, ne::Task<T> _task, const std::size_t _index, RaceState& _state, std::optional<WhenAnyResult<T>>& _result)
		{
			auto value = co_await std::move(_task);
			if (!_state.isDecided)
			{
				_state.isDecided = true;
				_result.emplace(WhenAnyResult<T>{ _index, std::move(value) });
				if (_state.outer) _executor.Post(_state.outer);
			}
		}
	}

	/**
	 * @brief 동종 태스크들을 동시에 구동해 가장 먼저 완료된 하나의 인덱스+값을 반환합니다.
	 *
	 * 승부가 나면 진 태스크들은 그대로 파괴되어 취소됩니다(진행 중 I/O 완료는 이벤트 루프가 배수,
	 * 미발화 타이머는 소멸자가 Cancel — Task 수명 계약). happy-eyeballs 처럼 여러 후보 연결을
	 * 경합시키거나, I/O 와 별도 이벤트를 겨루는 데 쓸 수 있습니다.
	 *
	 * @note 단일 스레드 실행자(예: 하나의 io::Context) 위에서 구동됨을 전제합니다. _tasks 는 비어 있으면 안 됩니다.
	 */
	template <typename T>
	[[nodiscard]] ne::Task<WhenAnyResult<T>> WhenAny(IExecutor& _executor, std::vector<ne::Task<T>> _tasks)
	{
		assert(!_tasks.empty() && "WhenAny: 최소 하나의 태스크가 필요합니다");

		RaceState state{};
		std::optional<WhenAnyResult<T>> result;

		// 레이서들을 코루틴 스코프에 살려둔다 — WhenAny 가 co_return 하며 이 벡터가 파괴될 때
		// 진 레이서(그리고 그가 소유한 태스크)가 함께 파괴되어 취소된다.
		std::vector<ne::Task<void_t>> racers;
		racers.reserve(_tasks.size());
		for (std::size_t i = 0; i < _tasks.size() && !state.isDecided; ++i)
		{
			racers.push_back(internal::WhenAnyRacer<T>(_executor, std::move(_tasks[i]), i, state, result));
			racers.back().Resume(); // 동기적으로 즉시 완료되면 isDecided 가 서고 다음 레이서는 시작하지 않는다
		}

		if (!state.isDecided) co_await AwaitDecision{ state };

		co_return std::move(*result);
	}
}
