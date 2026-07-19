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
#include "Io/Context/Context.h"

BEGIN_NS(ne::io)
	template <typename U>
	struct TaskValueType;

	template <typename U>
	struct TaskValueType<ne::Task<U>>
	{
		using type = U;
	};

	/**
	 * @class RaceState
	 * @brief Timeout() 내부에서 I/O 레이서와 타이머 레이서가 공유하는 경합 상태.
	 *
	 * 두 레이서 중 먼저 끝난 쪽이 isDecided 를 세팅하고 outer(Timeout 코루틴 핸들)를 깨운다.
	 *
	 * @note 단일 io::Context(단일 스레드) 실행을 전제하므로 원자적 동기화가 필요 없다.
	 */
	struct RaceState
	{
		std::coroutine_handle<> outer;
		bool_t isDecided{ false };
	};

	/**
	 * @class AwaitDecision
	 * @brief Timeout() 이 경합 결과를 기다리기 위해 co_await 하는 대기점.
	 *
	 * RaceState 가 이미 결정된 상태면 즉시 통과하고, 아니면 자신의 핸들을 RaceState 에 남기고
	 * suspend 한다. 이후 승리한 레이서가 Post 를 통해 재개시킨다.
	 */
	struct AwaitDecision
	{
		RaceState& state;

		[[nodiscard]] bool_t await_ready() const noexcept { return state.isDecided; }
		void_t await_suspend(const std::coroutine_handle<> _handle) noexcept { state.outer = _handle; }
		void_t await_resume() const noexcept {}
	};

	template <typename T>
	ne::Task<void_t> RaceIo(Context& _context, ne::Task<T> _task, RaceState& _state, std::optional<T>& _result)
	{
		auto value = co_await std::move(_task);
		if (!_state.isDecided)
		{
			_state.isDecided = true;
			_result.emplace(std::move(value));
			if (_state.outer) _context.Post(_state.outer);
		}
	}

	inline ne::Task<void_t> RaceTimer(Context& _context, const std::chrono::milliseconds _duration, RaceState& _state, std::stop_source& _source)
	{
		co_await _context.SleepFor(_duration);
		if (!_state.isDecided)
		{
			_state.isDecided = true;
			(void_t)_source.request_stop();
			if (_state.outer) _context.Post(_state.outer);
		}
	}

	template <typename Fn>
	[[nodiscard]] ne::Task<std::optional<typename TaskValueType<std::invoke_result_t<Fn, std::stop_token>>::type>> Timeout(Context& _context, std::chrono::milliseconds _duration, Fn _makeTask)
	{
		using T = typename TaskValueType<std::invoke_result_t<Fn, std::stop_token>>::type;

		std::stop_source source;
		RaceState state{};
		std::optional<T> result;

		auto ioRacer = RaceIo<T>(_context, _makeTask(source.get_token()), state, result);
		ioRacer.Resume();

		if (std::optional<ne::Task<void_t>> timerRacer; !state.isDecided)
		{
			timerRacer.emplace(RaceTimer(_context, _duration, state, source));
			timerRacer->Resume();
		}

		if (!state.isDecided) co_await AwaitDecision{ state };

		co_return std::move(result);
	}

END_NS
