//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <coroutine>
#include <optional>
#include <utility>
#include "Base/Type.h"

BEGIN_NS(ne)
	// 계약: 소멸자는 무조건 handle.destroy() 로 코루틴 프레임을 파괴한다 — 코루틴이 진행 중인
	// I/O 로 suspend 된 채(예: co_await Io::Awaitable/Time::Awaitable 내부) 소멸되어도 안전하다.
	// (예전엔 여기서 UAF 위험을 경고했으나, 지금은 그 완료 컨텍스트가 코루틴 프레임이 아니라
	// heap 에 별도로 살며 — Io::Awaitable 은 CompletionHandler 를 abandoned=true 로 표시해
	// 루프에 소유권을 넘기고, Time::Awaitable 은 소멸자가 미발화 타이머를 wheel.Cancel() 한다 —
	// 중도 폐기가 정상 경로다. Io/Coroutine/Timeout.h 의 when_any 류 콤비네이터가 진 쪽 Task 를
	// 그대로 파괴해 취소하는 것도 이 계약에 기대고 있다.)
	template <typename T>
	class Task
	{
	public:
		struct promise_type
		{
		private:
			// symmetric transfer: 완료 시 호출자 코루틴으로 즉시 이동 (스택 성장 없음)
			struct FinalAwaiter
			{
				bool_t await_ready() const noexcept { return false; }

				std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> _handle) noexcept
				{
					auto& promise = _handle.promise();
					return promise.continuation ? promise.continuation : std::noop_coroutine();
				}

				void_t await_resume() const noexcept {}
			};

		public:
			promise_type() = default;
			~promise_type() = default;

		public:
			std::optional<T> result;
			std::coroutine_handle<> continuation;

		public:
			Task get_return_object() noexcept { return Task{ std::coroutine_handle<promise_type>::from_promise(*this) }; }

			std::suspend_always initial_suspend() noexcept { return {}; }
			FinalAwaiter final_suspend() noexcept { return {}; }

			void_t return_value(T _value) noexcept { result.emplace(std::move(_value)); }
			void_t unhandled_exception() noexcept { std::terminate(); }

		public:
			T TakeResult() noexcept { return std::move(*result); }
			void_t SetContinuation(std::coroutine_handle<> _continuation) noexcept { continuation = _continuation; }
		};

	public:
		explicit Task(std::coroutine_handle<promise_type> _handle) noexcept
			: handle(_handle) {}

		~Task() noexcept { if (handle) handle.destroy(); }

		Task(Task&& _other) noexcept
			: handle(std::exchange(_other.handle, {})) {}

		Task& operator=(Task&& _other) noexcept
		{
			if (this != &_other)
			{
				if (handle) handle.destroy();
				handle = std::exchange(_other.handle, {});
			}
			return *this;
		}

		NEBULA_NON_COPYABLE(Task)

	private:
		std::coroutine_handle<promise_type> handle;

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return handle.done(); }

		std::coroutine_handle<promise_type> await_suspend(std::coroutine_handle<> _caller) noexcept
		{
			handle.promise().SetContinuation(_caller);
			return handle;
		}

		[[nodiscard]] T await_resume() noexcept { return handle.promise().TakeResult(); }

	public:
		void_t Resume() noexcept { if (handle && !handle.done()) handle.resume(); }

		[[nodiscard]] bool_t IsReady() const noexcept { return !handle || handle.done(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
	};


	template <>
	class Task<void_t>
	{
	public:
		struct promise_type
		{
		public:
			promise_type() = default;
			~promise_type() = default;

		private:
			struct FinalAwaiter
			{
				bool_t await_ready() const noexcept { return false; }

				std::coroutine_handle<> await_suspend(const std::coroutine_handle<promise_type> _handle) noexcept
				{
					const auto& promise = _handle.promise();
					return promise.continuation ? promise.continuation : std::noop_coroutine();
				}

				void_t await_resume() const noexcept {}
			};

			std::coroutine_handle<> continuation;

		public:
			Task get_return_object() noexcept { return Task{ std::coroutine_handle<promise_type>::from_promise(*this) }; }

			std::suspend_always initial_suspend() noexcept { return {}; }
			FinalAwaiter final_suspend() noexcept { return {}; }

			void_t return_void() noexcept {}
			void_t unhandled_exception() noexcept { std::terminate(); }

			void_t SetContinuation(const std::coroutine_handle<> _continuation) noexcept { continuation = _continuation; }
		};

	public:
		explicit Task(const std::coroutine_handle<promise_type> _handle) noexcept
			: handle(_handle) {}

		~Task() noexcept { if (handle) handle.destroy(); }

		Task(Task&& _other) noexcept
			: handle(std::exchange(_other.handle, {})) {}

		Task& operator=(Task&& _other) noexcept
		{
			if (this != &_other)
			{
				if (handle) handle.destroy();
				handle = std::exchange(_other.handle, {});
			}
			return *this;
		}

		NEBULA_NON_COPYABLE(Task)

	private:
		std::coroutine_handle<promise_type> handle;

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return handle.done(); }

		std::coroutine_handle<promise_type> await_suspend(const std::coroutine_handle<> _caller) noexcept
		{
			handle.promise().SetContinuation(_caller);
			return handle;
		}

		void_t await_resume() const noexcept {}

	public:
		void_t Resume() noexcept { if (handle && !handle.done()) handle.resume(); }

		[[nodiscard]] bool_t IsReady() const noexcept { return !handle || handle.done(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
	};

END_NS
