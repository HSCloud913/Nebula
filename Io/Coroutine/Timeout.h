//
// Created by hscloud on 26. 7. 10.
//

#pragma once
#include <chrono>
#include <coroutine>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>
#include "Base/Coroutine/Task.h"
#include "Io/Context.h"
#include "Base/Coroutine/Race.h"

namespace ne::io
{
	template <typename U>
	struct TaskValueType;

	template <typename U>
	struct TaskValueType<ne::Task<U>>
	{
		using type = U;
	};

	namespace internal
	{
		template <typename T>
		ne::Task<void_t> RaceIo(Context& _context, ne::Task<T> _task, ne::RaceState& _state, std::optional<T>& _result)
		{
			auto value = co_await std::move(_task);
			if (!_state.isDecided)
			{
				_state.isDecided = true;
				_result.emplace(std::move(value));
				if (_state.outer) _context.Post(_state.outer);
			}
		}

		inline ne::Task<void_t> RaceTimer(Context& _context, const ne::time::Deadline _deadline, ne::RaceState& _state, std::stop_source& _source)
		{
			co_await _context.SleepUntil(_deadline);
			if (!_state.isDecided)
			{
				_state.isDecided = true;
				(void_t)_source.request_stop();
				if (_state.outer) _context.Post(_state.outer);
			}
		}
	}

	/**
	 * @brief _makeTask 로 만든 I/O 태스크와 _deadline 타이머를 경합시킵니다.
	 *
	 * I/O 가 먼저 끝나면 그 값을 담은 optional 을, 타이머가 먼저 끝나면 nullopt 를 반환합니다.
	 * 타이머 승리 시 _makeTask 에 넘긴 stop_token 으로 취소를 요청하고, 진 쪽 태스크는 그대로
	 * 파괴되어(진행 중 I/O 는 루프가 배수) 취소됩니다.
	 *
	 * @note 데드라인을 **절대 시각**으로 받는 것이 핵심입니다. 상대 지연을 받으면 여러 단계를 거칠 때
	 * 각 단계가 자기 몫의 시간을 새로 부여받아 전체 예산이 단계 수만큼 늘어납니다. 같은 Deadline 을
	 * 여러 호출에 넘기면 그 전부가 하나의 예산을 공유합니다.
	 * @note 무기한 Deadline 이면 타이머 레이서를 아예 만들지 않습니다(불필요한 타이머 등록 회피).
	 */
	template <typename Fn>
	[[nodiscard]] ne::Task<std::optional<typename TaskValueType<std::invoke_result_t<Fn, std::stop_token>>::type>> Timeout(Context& _context, ne::time::Deadline _deadline, Fn _makeTask)
	{
		using T = typename TaskValueType<std::invoke_result_t<Fn, std::stop_token>>::type;

		std::stop_source source;
		ne::RaceState state{};
		std::optional<T> result;

		auto ioRacer = internal::RaceIo<T>(_context, _makeTask(source.get_token()), state, result);
		ioRacer.Resume();

		// timerRacer 는 반드시 함수(코루틴) 스코프에 둔다. if 초기화문에 두면 if 블록 종료 시 파괴되어
		// RaceTimer 코루틴이 destroy 되고, 그 안의 co_await 가 취소되어 타이머가 스케줄되자마자
		// 사라진다(→ 타이머가 영영 발화하지 않아 타이머 승리 경로가 멈춘다). AwaitDecision 이후까지 살려야 한다.
		std::optional<ne::Task<void_t>> timerRacer;
		if (!state.isDecided && _deadline.HasExpiry())
		{
			timerRacer.emplace(internal::RaceTimer(_context, _deadline, state, source));
			timerRacer->Resume();
		}

		if (!state.isDecided) co_await ne::AwaitDecision{ state };

		co_return std::move(result);
	}

	/** @brief 지금부터 _duration 뒤를 시한으로 삼는 편의 오버로드(단일 단계에만 시한을 걸 때). */
	template <typename Fn>
	[[nodiscard]] auto Timeout(Context& _context, const std::chrono::milliseconds _duration, Fn _makeTask)
	{
		return Timeout(_context, _context.DeadlineAfter(_duration), std::move(_makeTask));
	}
}
