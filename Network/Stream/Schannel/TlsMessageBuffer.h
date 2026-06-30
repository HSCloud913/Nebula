//
// Created by hscloud on 25. 6. 30.
//

#pragma once
#ifdef _WIN32

#include <cassert>
#include <span>
#include <vector>
#include "Type.h"

BEGIN_NS(ne::network)
	class TlsMessageBuffer final
	{
	public:
		TlsMessageBuffer() = default;
		~TlsMessageBuffer() = default;
		NEBULA_DEFAULT_MOVE(TlsMessageBuffer)
		NEBULA_NON_COPYABLE(TlsMessageBuffer)

	private:
		explicit TlsMessageBuffer(const std::size_t _size) : buffer(_size) {}

	public:
		std::span<ne::byte_t> data{};

	private:
		std::vector<ne::byte_t> buffer;

	public:
		void_t Resize(std::size_t _size);

		[[nodiscard]] std::span<ne::byte_t> GetBuffer() { return buffer; }
		[[nodiscard]] std::vector<ne::byte_t>::iterator Begin() { return buffer.begin(); }
		[[nodiscard]] std::vector<ne::byte_t>::iterator End() { return buffer.end(); }
		[[nodiscard]] static TlsMessageBuffer Allocate() { return TlsMessageBuffer(1 << 14); }
	};

END_NS
#endif // _WIN32
