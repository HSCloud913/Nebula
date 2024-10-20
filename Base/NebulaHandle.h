//
// Created by hsclo on 24. 5. 20.
//

#ifndef NEBULAHANDLE_H
#define NEBULAHANDLE_H

#include <type_traits>
#include "Type.h"

template <typename T>
concept IsTrivial = std::is_trivial_v<T>;

template <IsTrivial T, std::invocable<T&> Deleter, T InvalidHandle = T{}>
class NebulaHandle final
{
	NEBULA_NON_COPYABLE(NebulaHandle)

public:
	constexpr NebulaHandle() = default;
	constexpr ~NebulaHandle() noexcept
	{
		Close();
	}
	constexpr explicit NebulaHandle(const T _handle) noexcept
		: handle(_handle)
	{
	}
	constexpr NebulaHandle(NebulaHandle&& _nebulaHandle) noexcept
		: handle(_nebulaHandle.handle)
	{
		_nebulaHandle.handle = InvalidHandle;
	}
	constexpr NebulaHandle& operator=(const T _handle)
	{
		if (handle != _handle)
		{
			Close();
			handle = _handle;
		}

		return *this;
	}
	constexpr NebulaHandle& operator=(NebulaHandle&& _nebulaHandle) noexcept
	{
		if (this != &_nebulaHandle.handle)
		{
			handle = _nebulaHandle.handle;
			_nebulaHandle.handle = InvalidHandle;
		}

		return *this;
	}

private:
	T handle = InvalidHandle;

public:
	[[nodiscard]] constexpr explicit operator T() const noexcept
	{
		return handle;
	}
	[[nodiscard]] constexpr T Get() const noexcept
	{
		return handle;
	}
	[[nodiscard]] constexpr T& Get() noexcept
	{
		return handle;
	}
	[[nodiscard]] constexpr const T* operator->() const noexcept
	{
		return &handle;
	}
	[[nodiscard]] constexpr T* operator->() noexcept
	{
		return &handle;
	}
	[[nodiscard]] constexpr const T* operator&() const noexcept
	{
		return &handle;
	}
	[[nodiscard]] constexpr T* operator&() noexcept
	{
		return &handle;
	}
	[[nodiscard]] constexpr explicit operator ne::bool_t() const noexcept
	{
		return handle != InvalidHandle;
	}
	[[nodiscard]] constexpr ne::bool_t operator!() const noexcept
	{
		return handle == InvalidHandle;
	}
	[[nodiscard]] constexpr ne::bool_t operator==(const NebulaHandle&) const noexcept requires std::equality_comparable<T> = default;

private:
	constexpr ne::void_t Close()
	{
		if (handle != InvalidHandle)
		{
			try
			{
				Deleter{}(handle);
			} catch (...)
			{
				// log
			}

			handle = InvalidHandle;
		}
	}
};

#endif //NEBULAHANDLE_H
