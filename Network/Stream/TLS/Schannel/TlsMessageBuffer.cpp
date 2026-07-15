//
// Created by hscloud on 25. 6. 30.
//

#include "Network/Stream/TLS/Schannel/TlsMessageBuffer.h"



#ifdef _WIN32

BEGIN_NS(ne::network)
	void_t TlsMessageBuffer::Resize(const std::size_t _size)
	{
		assert(_size >= buffer.size());

		if (!data.empty())
		{
			const auto dataStart = data.data() - buffer.data();
			assert(dataStart > 0 && dataStart < static_cast<std::ptrdiff_t>(buffer.size()));

			buffer.resize(_size);
			data = std::span(buffer).subspan(static_cast<std::size_t>(dataStart), data.size());
		}
		else { buffer.resize(_size); }
	}

END_NS

#endif // _WIN32
