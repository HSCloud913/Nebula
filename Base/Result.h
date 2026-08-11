//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <cassert>
#include <functional>
#include <optional>
#include <variant>
#include <type_traits>
#include "Base/Type.h"
#include "Base/Error.h"

namespace ne
{
	/**
	 * @class Result
	 * @brief 실패할 수 있는 연산을 예외 없이 표현하는 타입입니다.
	 *
	 * 성공값(T)과 에러(E) 중 하나만 보유합니다. Value()/Error() 호출 전에 IsOk()/IsError()로
	 * 상태를 확인해야 하며, 잘못된 상태에서 접근하면 assert가 발생합니다. 조건 분기 없이 값을
	 * 변환/연결하려면 Map()/AndThen()/OrElse()/ValueOr() 모나딕 조합자를 사용하세요.
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

	private:
		// ── 모나딕 조합자 공통 구현 ──
		// deducing this(P0847, GCC 14+/MSVC 19.32+) 미지원 컴파일러에서도 빌드되도록, 아래 공개
		// 오버로드(&/const&/&&)가 이 정적 구현으로 위임한다. Self 는 Result&/const Result&/Result
		// 로 전달되고 std::get 이 그 값 카테고리를 그대로 넘겨주므로, rvalue 에서는 이동하고
		// move-only T 도 지원한다.

		template <typename Self, typename F>
		static auto MapImpl(Self&& _self, F&& _function)
		{
			using U = std::invoke_result_t<F, decltype(std::get<T>(std::forward<Self>(_self).storage))>;
			using Ret = Result<std::conditional_t<std::is_void_v<U>, void_t, U>, E>;

			if (_self.IsError()) return Ret::Error(std::get<E>(std::forward<Self>(_self).storage));
			if constexpr (std::is_void_v<U>)
			{
				std::invoke(std::forward<F>(_function), std::get<T>(std::forward<Self>(_self).storage));
				return Ret::Ok();
			}
			else return Ret::Ok(std::invoke(std::forward<F>(_function), std::get<T>(std::forward<Self>(_self).storage)));
		}

		template <typename Self, typename F>
		static auto AndThenImpl(Self&& _self, F&& _function)
		{
			using Ret = std::remove_cvref_t<std::invoke_result_t<F, decltype(std::get<T>(std::forward<Self>(_self).storage))>>;

			if (_self.IsError()) return Ret::Error(std::get<E>(std::forward<Self>(_self).storage));
			return std::invoke(std::forward<F>(_function), std::get<T>(std::forward<Self>(_self).storage));
		}

		template <typename Self, typename F>
		static auto OrElseImpl(Self&& _self, F&& _function)
		{
			using Ret = std::remove_cvref_t<std::invoke_result_t<F, decltype(std::get<E>(std::forward<Self>(_self).storage))>>;

			if (_self.IsOk()) return Ret::Ok(std::get<T>(std::forward<Self>(_self).storage));
			return std::invoke(std::forward<F>(_function), std::get<E>(std::forward<Self>(_self).storage));
		}

		template <typename Self, typename U>
		static T ValueOrImpl(Self&& _self, U&& _default)
		{
			if (_self.IsOk()) return std::get<T>(std::forward<Self>(_self).storage);
			return static_cast<T>(std::forward<U>(_default));
		}

	public:
		/** @brief Ok면 F(T)->U 로 값을 변환해 Result<U,E>를, Error면 에러를 그대로 전파한다. */
		template <typename F>
		[[nodiscard]] auto Map(F&& _function) & { return MapImpl(*this, std::forward<F>(_function)); }

		template <typename F>
		[[nodiscard]] auto Map(F&& _function) const & { return MapImpl(*this, std::forward<F>(_function)); }

		template <typename F>
		[[nodiscard]] auto Map(F&& _function) && { return MapImpl(std::move(*this), std::forward<F>(_function)); }

		/** @brief Ok면 F(T)->Result<U,E> 를 호출해 그 결과를, Error면 에러를 전파한다(실패 가능 연산 체이닝). */
		template <typename F>
		[[nodiscard]] auto AndThen(F&& _function) & { return AndThenImpl(*this, std::forward<F>(_function)); }

		template <typename F>
		[[nodiscard]] auto AndThen(F&& _function) const & { return AndThenImpl(*this, std::forward<F>(_function)); }

		template <typename F>
		[[nodiscard]] auto AndThen(F&& _function) && { return AndThenImpl(std::move(*this), std::forward<F>(_function)); }

		/** @brief Error면 F(E)->Result<T,E2> 로 복구를 시도하고, Ok면 값을 그대로 전달한다. */
		template <typename F>
		[[nodiscard]] auto OrElse(F&& _function) & { return OrElseImpl(*this, std::forward<F>(_function)); }

		template <typename F>
		[[nodiscard]] auto OrElse(F&& _function) const & { return OrElseImpl(*this, std::forward<F>(_function)); }

		template <typename F>
		[[nodiscard]] auto OrElse(F&& _function) && { return OrElseImpl(std::move(*this), std::forward<F>(_function)); }

		/** @brief Ok면 값을, Error면 _default 를 반환한다. */
		template <typename U>
		[[nodiscard]] T ValueOr(U&& _default) & { return ValueOrImpl(*this, std::forward<U>(_default)); }

		template <typename U>
		[[nodiscard]] T ValueOr(U&& _default) const & { return ValueOrImpl(*this, std::forward<U>(_default)); }

		template <typename U>
		[[nodiscard]] T ValueOr(U&& _default) && { return ValueOrImpl(std::move(*this), std::forward<U>(_default)); }

		/** @brief (rvalue 전용) Error 상태면 에러에 컨텍스트를 덧붙여 그대로 돌려준다. E 가 Context()를 가질 때만 활성화. */
		[[nodiscard]] Result Context(const string_view_t _context) &&
			requires requires(E& _error, string_view_t _ctx) { _error.Context(_ctx); }
		{
			if (IsError()) Error().Context(_context);
			return std::move(*this);
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

	private:
		// ── 모나딕 조합자 공통 구현 (값이 없으므로 F 는 인자를 받지 않는다) ──
		// 원본(Result<T,E>)과 동일한 이유로, 공개 오버로드(&/const&/&&)가 이 정적 구현에 위임한다.

		template <typename Self, typename F>
		static auto MapImpl(Self&& _self, F&& _function)
		{
			using U = std::invoke_result_t<F>;
			using Ret = Result<std::conditional_t<std::is_void_v<U>, void_t, U>, E>;

			if (_self.IsError()) return Ret::Error(*std::forward<Self>(_self).errorStorage);
			if constexpr (std::is_void_v<U>)
			{
				std::invoke(std::forward<F>(_function));
				return Ret::Ok();
			}
			else return Ret::Ok(std::invoke(std::forward<F>(_function)));
		}

		template <typename Self, typename F>
		static auto AndThenImpl(Self&& _self, F&& _function)
		{
			using Ret = std::remove_cvref_t<std::invoke_result_t<F>>;

			if (_self.IsError()) return Ret::Error(*std::forward<Self>(_self).errorStorage);
			return std::invoke(std::forward<F>(_function));
		}

		template <typename Self, typename F>
		static auto OrElseImpl(Self&& _self, F&& _function)
		{
			using Ret = std::remove_cvref_t<std::invoke_result_t<F, decltype(*std::forward<Self>(_self).errorStorage)>>;

			if (_self.IsOk()) return Ret::Ok();
			return std::invoke(std::forward<F>(_function), *std::forward<Self>(_self).errorStorage);
		}

	public:
		/** @brief Ok면 F()->U 를 호출해 Result<U,E>를, Error면 에러를 전파한다. */
		template <typename F>
		[[nodiscard]] auto Map(F&& _function) & { return MapImpl(*this, std::forward<F>(_function)); }

		template <typename F>
		[[nodiscard]] auto Map(F&& _function) const & { return MapImpl(*this, std::forward<F>(_function)); }

		template <typename F>
		[[nodiscard]] auto Map(F&& _function) && { return MapImpl(std::move(*this), std::forward<F>(_function)); }

		/** @brief Ok면 F()->Result<U,E> 를 호출해 그 결과를, Error면 에러를 전파한다(체이닝). */
		template <typename F>
		[[nodiscard]] auto AndThen(F&& _function) & { return AndThenImpl(*this, std::forward<F>(_function)); }

		template <typename F>
		[[nodiscard]] auto AndThen(F&& _function) const & { return AndThenImpl(*this, std::forward<F>(_function)); }

		template <typename F>
		[[nodiscard]] auto AndThen(F&& _function) && { return AndThenImpl(std::move(*this), std::forward<F>(_function)); }

		/** @brief Error면 F(E)->Result<void_t,E2> 로 복구를 시도하고, Ok면 그대로 성공을 전달한다. */
		template <typename F>
		[[nodiscard]] auto OrElse(F&& _function) & { return OrElseImpl(*this, std::forward<F>(_function)); }

		template <typename F>
		[[nodiscard]] auto OrElse(F&& _function) const & { return OrElseImpl(*this, std::forward<F>(_function)); }

		template <typename F>
		[[nodiscard]] auto OrElse(F&& _function) && { return OrElseImpl(std::move(*this), std::forward<F>(_function)); }

		/** @brief (rvalue 전용) Error 상태면 에러에 컨텍스트를 덧붙여 그대로 돌려준다. E 가 Context()를 가질 때만 활성화. */
		[[nodiscard]] Result Context(const string_view_t _context) &&
			requires requires(E& _error, string_view_t _ctx) { _error.Context(_ctx); }
		{
			if (IsError()) Error().Context(_context);
			return std::move(*this);
		}
	};
}
