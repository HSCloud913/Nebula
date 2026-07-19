//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstddef>
#include <span>
#include <cassert>
#include "Base/Type.h"

BEGIN_NS(ne::io)
	/**
	 * @class BufferView
	 * @brief 비소유 메모리 조각(포인터+길이)을 나타내는 값 타입.
	 *
	 * scatter/gather I/O(BufferChain)의 세그먼트 단위로 쓰인다. 메모리를 소유하지 않으며,
	 * 가리키는 메모리의 수명은 호출자가 I/O 완료 시점까지 보장해야 한다.
	 */
	struct BufferView
	{
		ne::byte_t* ptr{ nullptr };
		std::size_t length{};

		[[nodiscard]] BufferView Slice(const std::size_t _offset, const std::size_t _length) const noexcept
		{
			assert(_offset + _length <= length);
			return { ptr + _offset, _length };
		}

		[[nodiscard]] std::span<const ne::byte_t> Span() const noexcept { return { ptr, length }; }
		[[nodiscard]] bool_t IsValid() const noexcept { return ptr != nullptr; }
	};

END_NS
