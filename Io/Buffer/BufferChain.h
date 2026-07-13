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
	// BufferView 연결 리스트 — scatter/gather I/O 지원.
	class BufferChain
	{
	public:
		BufferChain() = default;

		NEBULA_DEFAULT_COPY_MOVE(BufferChain)

	private:
		std::vector<BufferView> segments;

	public:
		void_t Append(BufferView _view) { segments.push_back(_view); } // 경량 객체이므로 매개변수를 const 참조할 필요가 없음
		void_t Clear() { segments.clear(); }

		// 앞에서 _skipBytes 만큼 건너뛴 나머지를 새 체인으로 반환 (세그먼트 재슬라이스, 소유권 없음 —
		// BufferView 와 동일 원칙). Sendv() 가 partial write 를 반환했을 때 남은 부분만 재시도하는 용도.
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
