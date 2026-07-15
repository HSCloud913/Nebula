//
// Created by nebula on 24. 5. 20.
//

#pragma once
#include <type_traits>
#include <memory>
#include "Base/Type.h"

template <typename T>concept IsTrivial = std::is_trivial_v<T>;

BEGIN_NS(ne)
	/**
	 * @class Handle
	 * @brief 원시 핸들(fd, HANDLE, socket 등)을 소유하는 move-only RAII 래퍼입니다.
	 *
	 * 재할당 시점과 소멸 시점에 Deleter를 자동으로 호출해 핸들을 해제하며, 복사는 금지됩니다.
	 *
	 * @tparam T 핸들의 실제 값 타입입니다.
	 * @tparam Deleter `Deleter{}(handle)` 형태로 호출되어 핸들을 해제하는 functor입니다.
	 * @tparam InvalidHandle "소유한 핸들 없음"을 나타내는 sentinel 값입니다. (기본 생성 시 및 해제 후 상태)
	 */
	template <IsTrivial T, std::invocable<T&> Deleter, T InvalidHandle = T{}>
	class Handle final
	{
	public:
		constexpr Handle() = default;
		constexpr explicit Handle(const T _handle) noexcept
			: handle(_handle) {}

		constexpr ~Handle() noexcept { Close(); }

		constexpr Handle(const Handle& _nebulaHandle) = delete;
		constexpr Handle& operator=(const T _handle)
		{
			if (handle != _handle)
			{
				Close();
				handle = _handle;
			}

			return *this;
		}

		constexpr Handle(Handle&& _nebulaHandle) noexcept
			: handle(_nebulaHandle.handle) { _nebulaHandle.handle = InvalidHandle; }

		constexpr Handle& operator=(Handle&& _nebulaHandle) noexcept
		{
			if (this != std::addressof(_nebulaHandle))
			{
				Close();
				handle = _nebulaHandle.handle;
				_nebulaHandle.handle = InvalidHandle;
			}

			return *this;
		}

	private:
		T handle = InvalidHandle;

	public:
		[[nodiscard]] constexpr explicit operator T() const noexcept { return handle; }
		[[nodiscard]] constexpr T Get() const noexcept { return handle; }
		[[nodiscard]] constexpr T& Get() noexcept { return handle; }
		[[nodiscard]] constexpr const T* operator->() const noexcept { return &handle; }
		[[nodiscard]] constexpr T* operator->() noexcept { return &handle; }
		/** @brief `*this`의 주소가 아닌, 내부 handle 값의 주소를 반환합니다. (핸들에 직접 값을 써넣는 API용) */
		[[nodiscard]] constexpr const T* operator&() const noexcept { return &handle; }
		[[nodiscard]] constexpr T* operator&() noexcept { return &handle; }
		[[nodiscard]] constexpr explicit operator ne::bool_t() const noexcept { return handle != InvalidHandle; }
		[[nodiscard]] constexpr ne::bool_t operator!() const noexcept { return handle == InvalidHandle; }
		[[nodiscard]] constexpr ne::bool_t operator==(const Handle&) const noexcept requires std::equality_comparable<T> = default;

	private:
		constexpr ne::void_t Close() noexcept
		{
			if (handle != InvalidHandle)
			{
				Deleter{}(handle);
				handle = InvalidHandle;
			}
		}
	};

END_NS
