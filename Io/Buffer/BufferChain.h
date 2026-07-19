//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <cstddef>
#include <vector>
#include "Io/Buffer/BufferView.h"

#if defined(IS_POSIX)
#   include <sys/uio.h>
#endif

BEGIN_NS(ne::io)
	/**
	 * @class BufferChain
	 * @brief BufferView 들을 순서대로 엮은 scatter/gather I/O 버퍼 목록.
	 *
	 * 각 세그먼트는 비소유 BufferView 이며, BufferChain 자체도 메모리를 소유하지 않는다.
	 * Suffix() 로 앞부분을 건너뛴 나머지 체인을 재구성할 수 있어 partial write 재시도에 쓰인다.
	 */
	class BufferChain
	{
	public:
		BufferChain() = default;

		NEBULA_DEFAULT_COPY_MOVE(BufferChain)

	private:
		std::vector<BufferView> segments;

	public:
		void_t Append(BufferView _view) { segments.push_back(_view); }
		void_t Clear() { segments.clear(); }

		[[nodiscard]] BufferChain Suffix(const std::size_t _skipBytes) const
		{
			BufferChain result;
			std::size_t remaining = _skipBytes;

			for (const auto& segment : segments)
			{
				if (remaining == 0)
				{
					result.Append(segment);
					continue;
				}
				if (remaining >= segment.length)
				{
					remaining -= segment.length;
					continue;
				}

				result.Append(segment.Slice(remaining, segment.length - remaining));
				remaining = 0;
			}

			return result;
		}

		[[nodiscard]] std::size_t TotalSize() const noexcept
		{
			std::size_t total = 0;
			for (const auto& [ptr, length] : segments) total += length;

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

	public:
		[[nodiscard]] bool_t IsEmpty() const noexcept { return segments.empty(); }
		[[nodiscard]] const std::vector<BufferView>& Segments() const noexcept { return segments; }
	};

END_NS
