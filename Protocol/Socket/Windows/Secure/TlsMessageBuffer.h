//
// Created by nebula on 24. 5. 29.
//

#ifndef TLSMESSAGEBUFFER_H
#define TLSMESSAGEBUFFER_H

#include <cassert>
#include <span>
#include <vector>
#include "Type.h"

BEGIN_NS(ne::protocol)
	class TlsMessageBuffer final
	{
		NEBULA_NON_COPYABLE(TlsMessageBuffer)

	public:
		TlsMessageBuffer() = default;
		~TlsMessageBuffer() = default;

		NEBULA_DEFAULT_MOVE(TlsMessageBuffer)

	private:
		explicit TlsMessageBuffer(const std::size_t _size) : buffer(_size) {}

	private:
		std::vector<std::byte> buffer;

	public:
		std::span<std::byte> data;

	public:
		void_t Resize(std::size_t _size);

		[[nodiscard]] std::span<std::byte> GetBuffer() { return buffer; }
		[[nodiscard]] std::vector<std::byte>::iterator Begin() { return buffer.begin(); }
		[[nodiscard]] std::vector<std::byte>::iterator End() { return buffer.end(); }
		[[nodiscard]] static TlsMessageBuffer Allocate() { return TlsMessageBuffer(1 << 14); }
	};

END_NS

#endif //TLSMESSAGEBUFFER_H
