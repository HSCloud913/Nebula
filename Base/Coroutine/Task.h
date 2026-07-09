//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <coroutine>
#include <optional>
#include <utility>
#include "Base/Type.h"

BEGIN_NS(ne)
	// 계약(중요): Task 는 완료(IsReady()) 되었거나 initial_suspend 이후 한 번도 재개되지
	// 않은 상태에서만 소멸/이동-대입해도 안전하다. 소멸자는 무조건 handle.destroy() 로
	// 코루틴 프레임을 파괴하므로, 코루틴이 "진행 중인 Proactor I/O"(SendSubmit/ReceiveSubmit/
	// SubmitTransmitFile 등, IoContext 를 코루틴 프레임 안에 두고 그 주소를 커널에 넘긴 상태)
	// 에서 suspend 된 채 소멸되면, 이후 엔진이 완료를 회수하며 이미 해제된 IoContext 를
	// 참조/resume 해 UAF 가 된다. 취소/타임아웃 경로에서 Task 를 중도 폐기하려면 먼저
	// 해당 I/O 의 완료를 회수(또는 취소-후-완료 대기)해야 한다.
	//   ↔ Reactor 경로(WatchEntry)는 엔진 소유라 이 문제가 없다. Proactor 의 IoContext 도
	//     엔진 소유로 옮기면(현재 프레임 소유) 대칭적으로 안전해진다 — 후속 과제.
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

				void_t await_resume() const noexcept {}
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

			void_t return_value(T _value) noexcept { result.emplace(std::move(_value)); }
			void_t unhandled_exception() noexcept { std::terminate(); }

			T TakeResult() noexcept { return std::move(*result); }
			void_t SetContinuation(std::coroutine_handle<> _continuation) noexcept { continuation = _continuation; }
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

		void_t Resume() noexcept
		{
			if (handle && !handle.done()) handle.resume();
		}

	public:
		[[nodiscard]] bool_t IsReady() const noexcept { return !handle || handle.done(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
	};


	template <>
	class Task<void_t>
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

				void_t await_resume() const noexcept {}
			};

			std::coroutine_handle<> continuation;

		public:
			Task get_return_object() noexcept
			{
				return Task{ std::coroutine_handle<promise_type>::from_promise(*this) };
			}

			std::suspend_always initial_suspend() noexcept { return {}; }
			FinalAwaiter final_suspend() noexcept { return {}; }

			void_t return_void() noexcept {}
			void_t unhandled_exception() noexcept { std::terminate(); }

			void_t SetContinuation(const std::coroutine_handle<> _continuation) noexcept { continuation = _continuation; }
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

		void_t await_resume() const noexcept {}

		void_t Resume() noexcept
		{
			if (handle && !handle.done()) handle.resume();
		}

	public:
		[[nodiscard]] bool_t IsReady() const noexcept { return !handle || handle.done(); }
		[[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }
	};

END_NS
