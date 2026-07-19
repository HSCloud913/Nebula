//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <coroutine>
#include <optional>
#include <utility>
#include "Base/Type.h"

BEGIN_NS(ne)
	/**
	 * @class Task
	 * @brief co_await 가능한 코루틴 반환 타입입니다. move-only이며, symmetric transfer로 완료 시
	 * 호출자 코루틴으로 즉시 이동합니다(스택 성장 없음).
	 *
	 * @note 계약: 소멸자는 무조건 handle.destroy() 로 코루틴 프레임을 파괴합니다 — 코루틴이 진행 중인
	 * I/O 로 suspend 된 채(예: co_await Io::Awaitable/Time::Awaitable 내부) 소멸되어도 안전합니다.
	 * 완료 컨텍스트가 코루틴 프레임이 아니라 heap 에 별도로 살기 때문입니다 — Io::Awaitable 은
	 * CompletionHandler 를 abandoned=true 로 표시해 루프에 소유권을 넘기고, Time::Awaitable 은
	 * 소멸자가 미발화 타이머를 wheel.Cancel() 합니다. 중도 폐기가 정상 경로이며, Io/Coroutine/Timeout.h
	 * 의 when_any 류 콤비네이터가 진 쪽 Task 를 그대로 파괴해 취소하는 것도 이 계약에 기대고 있습니다.
	 *
	 * @tparam T 코루틴이 co_return하는 값의 타입. 값이 없는 경우 아래 Task<void_t> 특수화를 사용합니다.
	 */
	template <typename T>
	class Task
	{
	public:
		struct promise_type
		{
		private:
			/** @brief symmetric transfer: 완료 시 호출자 코루틴으로 즉시 이동합니다. (스택 성장 없음) */
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
			/** @brief co_return 된 결과값을 소유권째로 꺼냅니다. 호출 후 result 는 빈 상태가 됩니다. */
			T TakeResult() noexcept { return std::move(*result); }

			/** @brief 완료 시 resume 할 호출자 코루틴 handle 을 등록합니다(symmetric transfer 용). */
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
		/** @brief 코루틴을 재개합니다. 이미 완료됐거나 handle 이 없으면 아무 일도 하지 않습니다. */
		void_t Resume() noexcept { if (handle && !handle.done()) handle.resume(); }

		/** @brief 코루틴이 완료(co_return)되어 결과를 꺼낼 수 있는 상태인지 확인합니다. */
		[[nodiscard]] bool_t IsReady() const noexcept { return !handle || handle.done(); }

		/** @brief move-out 등으로 handle 을 잃지 않은, 유효한 Task 인지 확인합니다. */
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
	};


	/** @brief 값을 반환하지 않는 코루틴을 위한 Task 특수화입니다. */
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
			/** @brief symmetric transfer: 완료 시 호출자 코루틴으로 즉시 이동합니다. (스택 성장 없음) */
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

			/** @brief 완료 시 resume 할 호출자 코루틴 handle 을 등록합니다(symmetric transfer 용). */
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
		/** @brief 코루틴을 재개합니다. 이미 완료됐거나 handle 이 없으면 아무 일도 하지 않습니다. */
		void_t Resume() noexcept { if (handle && !handle.done()) handle.resume(); }

		/** @brief 코루틴이 완료(co_return)됐는지 확인합니다. */
		[[nodiscard]] bool_t IsReady() const noexcept { return !handle || handle.done(); }

		/** @brief move-out 등으로 handle 을 잃지 않은, 유효한 Task 인지 확인합니다. */
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
	};

END_NS
