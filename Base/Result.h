//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <optional>
#include <variant>
#include <type_traits>
#include "Type.h"
#include "Error.h"

BEGIN_NS(ne)
	// ─── Result<T, E> ────────────────────────────────────────────────────────────
	// 성공값(T) 또는 에러(E)를 반환하는 타입.
	// void 결과는 Result<void, E> 특수화 사용.

	template <typename T, typename E = ne::Error>
	class Result
	{
	private:
		static_assert(!std::is_same_v<T, void>, "void result must use Result<void, E> specialization");
		static_assert(!std::is_same_v<T, E>, "value type and error type must differ");

	public:
		NEBULA_DEFAULT_COPY(Result)
		NEBULA_DEFAULT_MOVE(Result)

	private:
		explicit Result(T _value) : storage(std::move(_value)) {}
		explicit Result(E _error) : storage(std::move(_error)) {}

	private:
		std::variant<T, E> storage;

	public:
		[[nodiscard]] static Result Ok(T _value) { return Result(std::move(_value)); }
		[[nodiscard]] static Result Error(E _error) { return Result(std::move(_error)); }

		[[nodiscard]] bool_t IsOk() const noexcept { return std::holds_alternative<T>(storage); }
		[[nodiscard]] bool_t IsError() const noexcept { return !std::holds_alternative<T>(storage); }
		[[nodiscard]] explicit operator bool_t() const noexcept { return IsOk(); }

		[[nodiscard]] T& Value() noexcept { return *std::get_if<T>(&storage); } // 호출 전 IsOk() 확인 필수. 에러 상태에서 호출하면 UB.
		[[nodiscard]] const T& Value() const noexcept { return *std::get_if<T>(&storage); } // 호출 전 IsOk() 확인 필수. 에러 상태에서 호출하면 UB.

		[[nodiscard]] E& Error() noexcept { return *std::get_if<E>(&storage); } // 호출 전 IsError() 확인 필수. 성공 상태에서 호출하면 UB.
		[[nodiscard]] const E& Error() const noexcept { return *std::get_if<E>(&storage); } // 호출 전 IsError() 확인 필수. 성공 상태에서 호출하면 UB.
	};

	template <typename E>
	class Result<void, E>
	{
	public:
		NEBULA_DEFAULT_COPY(Result)
		NEBULA_DEFAULT_MOVE(Result)

	private:
		Result() = default;
		explicit Result(E _error) : errorStorage(std::move(_error)) {}

	private:
		std::optional<E> errorStorage;

	public:
		[[nodiscard]] static Result Ok() { return Result{}; }
		[[nodiscard]] static Result Error(E _error) { return Result(std::move(_error)); }

		[[nodiscard]] bool_t IsOk() const noexcept { return !errorStorage.has_value(); }
		[[nodiscard]] bool_t IsError() const noexcept { return errorStorage.has_value(); }
		[[nodiscard]] explicit operator bool_t() const noexcept { return IsOk(); }

		[[nodiscard]] E& Error() noexcept { return *errorStorage; } // 호출 전 IsError() 확인 필수.
		[[nodiscard]] const E& Error() const noexcept { return *errorStorage; } // 호출 전 IsError() 확인 필수.
	};

END_NS
