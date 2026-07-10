//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstddef>
#include <memory_resource>
#include "Base/Type.h"

BEGIN_NS(ne::memory)
	class IAllocator :public std::pmr::memory_resource
	{
	public:
		IAllocator() = default;
		virtual ~IAllocator() override = default;

		NEBULA_NON_COPYABLE_MOVABLE(IAllocator)

	public:
		[[nodiscard]] virtual void_t* Allocate(std::size_t _size, std::size_t _align) = 0;
		virtual void_t Deallocate(void_t* _ptr, std::size_t _size) noexcept = 0;
		[[nodiscard]] virtual std::size_t Available() const noexcept = 0;

	protected:
		virtual void_t* do_allocate(const std::size_t _bytes, const std::size_t _align) override { return Allocate(_bytes, _align); }
		virtual void_t do_deallocate(void_t* _ptr, const std::size_t _bytes, std::size_t) noexcept override { Deallocate(_ptr, _bytes); }
		[[nodiscard]] virtual bool_t do_is_equal(const std::pmr::memory_resource& _other) const noexcept override { return this == &_other; }
	};

END_NS
