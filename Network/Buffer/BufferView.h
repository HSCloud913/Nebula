//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstddef>
#include <span>
#include <cassert>
#include "BufferBlock.h"
#include "Type.h"

BEGIN_NS(ne::network)
	// BufferBlock 슬라이스 뷰 (소유권 없음, 복사 없음).
	struct BufferView
	{
		BufferBlock* owner{ nullptr };
		ne::byte_t* ptr{ nullptr };
		std::size_t length{};

		[[nodiscard]] BufferView Slice(const std::size_t _offset, const std::size_t _length) const noexcept
		{
			assert(_offset + _length <= length);
			return { owner, ptr + _offset, _length };
		}

		[[nodiscard]] std::span<const ne::byte_t> Span() const noexcept { return { ptr, length }; }

		[[nodiscard]] bool_t IsValid() const noexcept { return owner != nullptr; }
	};

END_NS
