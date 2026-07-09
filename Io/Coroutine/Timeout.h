//
// Created by hscloud on 26. 7. 10.
//
// Level 2 — I/O 작업과 데드라인을 경합시키는 콤비네이터. time::Awaitable(SleepFor)은 이미
// Context::SleepFor 로 결선돼 있고, Io::Awaitable 은 stop_token 취소를 지원하며 Socket/File
// 공개 API 가 그 stop_token 을 통과시킨다 — 이 둘을 묶어 "I/O 대 데드라인" 경합을 표현한다.
// 완전 범용 N-way when_any 가 아니라, connect/read/write 타임아웃이라는 구체적 요구에 맞춘
// 2-way 경합(I/O 작업 하나 vs 타이머 하나)이다.
//
// 사용:
//   auto result = co_await Timeout(context, 5s,
//       [&](std::stop_token _token) { return socket.Receive(buffer, _token); });
//   if (!result) { /* 타임아웃 — request_stop() 으로 취소 요청은 이미 보냈다(RIO 경로 제외) */ }
//   else if (result->IsError()) { /* I/O 자체 에러(취소로 인한 aborted 포함) */ }
//
// 안전성: 진 쪽 Task 는 Timeout 이 반환할 때 지역변수 소멸자로 파괴된다 — Task 의 소멸자는
// 무조건 handle.destroy() 하므로 진행 중(suspend 상태) 프레임이 그대로 파괴되는데, 이게
// 안전한 이유는 새 안전장치가 아니라 기존 계약 재사용이다(Base/Coroutine/Task.h 계약 참고):
//   - I/O 가 진 경우: 내부 io::Awaitable 의 소멸자가 완료 전이면 handler->isAbandoned=true 로
//     소유권을 루프에 넘긴다(UAF 없음) — request_stop() 이 먼저 실제 커널 취소도 시도한다.
//   - 타이머가 진 경우: 내부 time::Awaitable 의 소멸자가 미발화 타이머를 wheel.Cancel() 한다.

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

	// 두 레이서(I/O 쪽/타이머 쪽) 중 먼저 끝난 쪽만 outer 를 깨우게 하는 공유 상태.
	// 단일 io::Context(단일 스레드) 전제 — 원자적 동기화가 필요 없다(Io/Buffer/BufferPool.h 와
	// 동일 전제).
	struct RaceState
	{
		std::coroutine_handle<> outer;
		bool_t isDecided{ false };
	};

	// Timeout() 이 co_await 하는 대기점 — 호출 시점에 이미 결판났으면(둘 중 하나가 동기
	// 완료) 그대로 통과하고, 아니면 자신의 handle 을 RaceState 에 남기고 suspend 한다
	// (나중에 레이서가 깨운다).
	struct AwaitDecision
	{
		RaceState& state;

		[[nodiscard]] bool_t await_ready() const noexcept { return state.isDecided; }
		void_t await_suspend(const std::coroutine_handle<> _handle) noexcept { state.outer = _handle; }
		void_t await_resume() const noexcept {}
	};

	template <typename T>
	ne::Task<void_t> RaceIo(ne::Task<T> _task, RaceState& _state, std::optional<T>& _result)
	{
		auto value = co_await std::move(_task);
		if (!_state.isDecided)
		{
			_state.isDecided = true;
			_result.emplace(std::move(value));
			if (_state.outer) _state.outer.resume();
		}
	}

	inline ne::Task<void_t> RaceTimer(Context& _context, const std::chrono::milliseconds _duration, RaceState& _state, std::stop_source& _source)
	{
		co_await _context.SleepFor(_duration);
		if (!_state.isDecided)
		{
			_state.isDecided = true;
			_source.request_stop(); // I/O 쪽 stop_token 이 살아있으면 진행 중인 op 을 커널 취소 요청
			if (_state.outer) _state.outer.resume();
		}
	}

	// _makeTask(stopToken) 로 I/O Task 를 만들어 _duration 과 경합시킨다. I/O 가 먼저 끝나면 그
	// 결과값을, 타이머가 먼저 끝나면 std::nullopt 를 반환한다.
	// _makeTask 는 값으로 받는다(코루틴 프레임에 참조를 남기면 안 되므로) — 람다가 캡처한
	// 참조(소켓/버퍼 등)의 수명은 호출자가 이 Timeout() 완료까지 보장해야 한다.
	template <typename Fn>
	[[nodiscard]] ne::Task<std::optional<typename TaskValueType<std::invoke_result_t<Fn, std::stop_token>>::type>>
	Timeout(Context& _context, std::chrono::milliseconds _duration, Fn _makeTask)
	{
		using T = typename TaskValueType<std::invoke_result_t<Fn, std::stop_token>>::type;

		std::stop_source source;
		RaceState state{};
		std::optional<T> result;

		auto ioRacer = RaceIo<T>(_makeTask(source.get_token()), state, result);
		ioRacer.Resume(); // I/O 제출까지 진행(첫 suspend 지점까지) — 동기 완료면 여기서 이미 decided=true

		// 타이머는 I/O 가 이미 동기 완료된 게 아닐 때만 등록한다(불필요한 스케줄/취소 방지).
		std::optional<ne::Task<void_t>> timerRacer;
		if (!state.isDecided)
		{
			timerRacer.emplace(RaceTimer(_context, _duration, state, source));
			timerRacer->Resume();
		}

		if (!state.isDecided) co_await AwaitDecision{ state };

		co_return std::move(result);
	}

END_NS
