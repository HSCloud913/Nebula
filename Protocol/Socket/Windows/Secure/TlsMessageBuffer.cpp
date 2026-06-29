//
// Created by nebula on 24. 5. 29.
//

#include "TlsMessageBuffer.h"



BEGIN_NS(ne::protocol)
	void_t TlsMessageBuffer::Resize(const std::size_t _size)
	{
		assert(_size >= buffer.size());

		if (!data.empty())
		{
			const auto dataStart = data.data() - buffer.data();
			assert(dataStart > 0 && dataStart < static_cast<std::ptrdiff_t>(buffer.size()));

			buffer.resize(_size);
			data = std::span(buffer).subspan(dataStart, data.size());
		}
		else
		{
			buffer.resize(_size);
		}
	}

END_NS
