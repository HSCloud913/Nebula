//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cassert>
#include <optional>
#include <variant>
#include <type_traits>
#include "Base/Type.h"
#include "Base/Error.h"

BEGIN_NS(ne)
	/**
	 * @class Result
	 * @brief 실패할 수 있는 연산을 예외 없이 표현하는 타입입니다.
	 *
	 * 성공값(T)과 에러(E) 중 하나만 보유합니다. Value()/Error() 호출 전에 IsOk()/IsError()로
	 * 상태를 확인해야 하며, 잘못된 상태에서 접근하면 assert가 발생합니다.
	 *
	 * @tparam T 성공 시의 값 타입입니다.
	 * @tparam E 실패 시의 에러 타입입니다. 기본값은 ne::Error입니다.
	 * @note 성공 시 값이 없는 경우에는 아래의 Result<void_t, E> 특수화를 사용하세요.
	 */
	template <typename T, typename E = ne::Error>
	class Result
	{
		static_assert(!std::is_same_v<T, void_t>, "void result must use Result<void, E> specialization");
		static_assert(!std::is_same_v<T, E>, "value type and error type must differ");

	private:
		explicit Result(T _value)
			: storage(std::move(_value)) {}

		explicit Result(E _error)
			: storage(std::move(_error)) {}

	public:
		NEBULA_DEFAULT_COPY_MOVE(Result)

	private:
		std::variant<T, E> storage;

	public:
		[[nodiscard]] static Result Ok(T _value) { return Result(std::move(_value)); }
		[[nodiscard]] static Result Error(E _error) { return Result(std::move(_error)); }

		[[nodiscard]] bool_t IsOk() const noexcept { return std::holds_alternative<T>(storage); }
		[[nodiscard]] bool_t IsError() const noexcept { return !std::holds_alternative<T>(storage); }
		[[nodiscard]] explicit operator bool_t() const noexcept { return IsOk(); }

		[[nodiscard]] T& Value() noexcept
		{
			assert(IsOk() && "Result::Value() called in error state");
			return *std::get_if<T>(&storage);
		}

		[[nodiscard]] const T& Value() const noexcept
		{
			assert(IsOk() && "Result::Value() called in error state");
			return *std::get_if<T>(&storage);
		}

		[[nodiscard]] E& Error() noexcept
		{
			assert(IsError() && "Result::Error() called in ok state");
			return *std::get_if<E>(&storage);
		}

		[[nodiscard]] const E& Error() const noexcept
		{
			assert(IsError() && "Result::Error() called in ok state");
			return *std::get_if<E>(&storage);
		}
	};

	template <typename E>
	class Result<void_t, E>
	{
	private:
		Result() = default;
		explicit Result(E _error)
			: errorStorage(std::move(_error)) {}

	public:
		NEBULA_DEFAULT_COPY_MOVE(Result)

	private:
		std::optional<E> errorStorage;

	public:
		[[nodiscard]] static Result Ok() { return Result{}; }
		[[nodiscard]] static Result Error(E _error) { return Result(std::move(_error)); }

		[[nodiscard]] bool_t IsOk() const noexcept { return !errorStorage.has_value(); }
		[[nodiscard]] bool_t IsError() const noexcept { return errorStorage.has_value(); }
		[[nodiscard]] explicit operator bool_t() const noexcept { return IsOk(); }

		[[nodiscard]] E& Error() noexcept
		{
			assert(IsError() && "Result::Error() called in ok state");
			return *errorStorage;
		}

		[[nodiscard]] const E& Error() const noexcept
		{
			assert(IsError() && "Result::Error() called in ok state");
			return *errorStorage;
		}
	};

END_NS
