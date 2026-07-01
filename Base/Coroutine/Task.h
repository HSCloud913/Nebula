//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <coroutine>
#include <optional>
#include <utility>
#include "Type.h"

BEGIN_NS(ne)
	template <typename T>
	class Task
	{
	public:
		struct promise_type; // ctor 시그니처 사용을 위한 전방 선언

	public:
		explicit Task(std::coroutine_handle<promise_type> _handle) noexcept
			: handle(_handle) {}

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

		~Task() noexcept { if (handle) handle.destroy(); }

		NEBULA_NON_COPYABLE(Task)

	public:
		struct promise_type
		{
		public:
			promise_type() = default;
			~promise_type() = default;

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

				void await_resume() const noexcept {}
			};

			std::optional<T> result;
			std::coroutine_handle<> continuation;

		public:
			Task get_return_object() noexcept
			{
				return Task{ std::coroutine_handle<promise_type>::from_promise(*this) };
			}

			std::suspend_always initial_suspend() noexcept { return {}; }
			FinalAwaiter final_suspend() noexcept { return {}; }

			void return_value(T _value) noexcept { result.emplace(std::move(_value)); }
			void unhandled_exception() noexcept { std::terminate(); }

			T TakeResult() noexcept { return std::move(*result); }
			void SetContinuation(std::coroutine_handle<> _continuation) noexcept { continuation = _continuation; }
		};

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

		void Resume() noexcept
		{
			if (handle && !handle.done()) handle.resume();
		}

	public:
		[[nodiscard]] bool_t IsReady() const noexcept { return !handle || handle.done(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
	};


	template <>
	class Task<void>
	{
	public:
		struct promise_type;

	public:
		explicit Task(const std::coroutine_handle<promise_type> _handle) noexcept
			: handle(_handle) {}

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

		~Task() noexcept { if (handle) handle.destroy(); }

		NEBULA_NON_COPYABLE(Task)

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

				void await_resume() const noexcept {}
			};

			std::coroutine_handle<> continuation;

		public:
			Task get_return_object() noexcept
			{
				return Task{ std::coroutine_handle<promise_type>::from_promise(*this) };
			}

			std::suspend_always initial_suspend() noexcept { return {}; }
			FinalAwaiter final_suspend() noexcept { return {}; }

			void return_void() noexcept {}
			void unhandled_exception() noexcept { std::terminate(); }

			void SetContinuation(const std::coroutine_handle<> _continuation) noexcept { continuation = _continuation; }
		};

	private:
		std::coroutine_handle<promise_type> handle;

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return handle.done(); }

		std::coroutine_handle<promise_type> await_suspend(const std::coroutine_handle<> _caller) noexcept
		{
			handle.promise().SetContinuation(_caller);
			return handle;
		}

		void await_resume() const noexcept {}

		void Resume() noexcept
		{
			if (handle && !handle.done()) handle.resume();
		}

	public:
		[[nodiscard]] bool_t IsReady() const noexcept { return !handle || handle.done(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
	};

END_NS
