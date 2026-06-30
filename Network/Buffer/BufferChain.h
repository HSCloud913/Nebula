//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstddef>
#include <cstring>
#include <vector>
#include "BufferView.h"
#include "Allocator/IAllocator.h"
#include "Type.h"

#if defined(IS_POSIX)
#   include <sys/uio.h>
#endif

BEGIN_NS(ne::network)
	// BufferView 연결 리스트 — scatter/gather I/O 지원.
	class BufferChain
	{
	public:
		BufferChain() = default;

		NEBULA_DEFAULT_COPY_MOVE(BufferChain)

	private:
		std::vector<BufferView> segments;

	public:
		void Append(BufferView _view) { segments.push_back(_view); } // 경량 객체이므로 매개변수를 const 참조할 필요가 없음
		void Clear() { segments.clear(); }

		[[nodiscard]] std::size_t TotalSize() const noexcept
		{
			std::size_t total = 0;
			for (const auto& segment : segments) total += segment.length;
			return total;
		}

#if defined(IS_POSIX)
		[[nodiscard]] std::vector<iovec> AsIovec() const
		{
			std::vector<iovec> v;
			v.reserve(segments.size());
			for (const auto& segment : segments) v.push_back({ segment.ptr, segment.length });
			return v;
		}
#endif

		[[nodiscard]] BufferView Flatten(ne::memory::IAllocator& _pool) const
		{ // 연속 메모리가 필요할 때만 사용.
			const std::size_t totalSize = TotalSize();

			auto rawBuffer = BufferBlock::Acquire(_pool, totalSize);
			if (rawBuffer.IsError()) return {};

			BufferBlock* buffer = rawBuffer.Value();
			ne::byte_t* destination = buffer->Data().data();
			for (const auto& segment : segments)
			{
				std::memcpy(destination, segment.ptr, segment.length);
				destination += segment.length;
			}

			return { buffer, buffer->Data().data(), totalSize };
		}

	public:
		[[nodiscard]] bool_t IsEmpty() const noexcept { return segments.empty(); }
		[[nodiscard]] const std::vector<BufferView>& Segments() const noexcept { return segments; }
	};

END_NS
