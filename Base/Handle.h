//
// Created by nebula on 24. 5. 20.
//

#pragma once
#include <type_traits>
#include <memory>
#include "Base/Type.h"

template <typename T>
concept IsTrivial = std::is_trivial_v<T>;

BEGIN_NS(ne)
	template <IsTrivial T, std::invocable<T&> Deleter, T InvalidHandle = T{}>
	class Handle final
	{
	public:
		constexpr Handle() = default;
		constexpr ~Handle() noexcept { Close(); }

		constexpr explicit Handle(const T _handle) noexcept
			: handle(_handle) {}

		constexpr Handle(Handle&& _nebulaHandle) noexcept
			: handle(_nebulaHandle.handle) { _nebulaHandle.handle = InvalidHandle; }

		constexpr Handle& operator=(const T _handle)
		{
			if (handle != _handle)
			{
				Close();
				handle = _handle;
			}

			return *this;
		}

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

		NEBULA_NON_COPYABLE(Handle)

	private:
		T handle = InvalidHandle;

	public:
		[[nodiscard]] constexpr explicit operator T() const noexcept { return handle; }
		[[nodiscard]] constexpr T Get() const noexcept { return handle; }
		[[nodiscard]] constexpr T& Get() noexcept { return handle; }
		[[nodiscard]] constexpr const T* operator->() const noexcept { return &handle; }
		[[nodiscard]] constexpr T* operator->() noexcept { return &handle; }
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
