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
	// ─── Result<T, E> ────────────────────────────────────────────────────────────
	// 성공값(T) 또는 에러(E)를 반환하는 타입.
	// void 결과는 Result<void, E> 특수화 사용.

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
