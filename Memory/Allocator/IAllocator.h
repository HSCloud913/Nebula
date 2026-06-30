//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstddef>
#include <memory_resource>
#include "Type.h"

BEGIN_NS(ne::memory)
	class IAllocator : public std::pmr::memory_resource
	{
	public:
		IAllocator() = default;
		virtual ~IAllocator() override = default;

		NEBULA_NON_COPYABLE_MOVABLE(IAllocator)

	public:
		[[nodiscard]] virtual void* Allocate(std::size_t _size, std::size_t _align) = 0;
		virtual void  Deallocate(void* _ptr, std::size_t _size) noexcept = 0;
		[[nodiscard]] virtual std::size_t Available() const noexcept = 0;

	protected:
		virtual void* do_allocate(const std::size_t _bytes, const std::size_t _align) override { return Allocate(_bytes, _align); }
		virtual void do_deallocate(void* _ptr, const std::size_t _bytes, std::size_t) noexcept override { Deallocate(_ptr, _bytes); }
		[[nodiscard]] virtual bool do_is_equal(const std::pmr::memory_resource& _other) const noexcept override { return this == &_other; }
	};
END_NS
