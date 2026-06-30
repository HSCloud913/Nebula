//
// Created by hscloud on 26. 6. 30.
//

#include "SendFile.h"

#if defined(_WIN32)
#	include <mswsock.h>
#elif defined(IS_POSIX)
#   include <sys/sendfile.h>
#   include <unistd.h>
#endif



BEGIN_NS(ne::io)
	ne::Task<ne::Result<std::size_t, ne::OsError>>
	SendFile(ne::network::socket_t _sockFd, int _fileFd, const std::size_t _offset, const std::size_t _size)
	{
#if defined(_WIN32)
		const auto handle = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(_fileFd));

		OVERLAPPED ov{};
		ov.Offset     = static_cast<DWORD>(_offset & 0xFFFFFFFF);
		ov.OffsetHigh = static_cast<DWORD>(_offset >> 32);

		if (!::TransmitFile(_sockFd, handle, _size, 0, &ov, nullptr, 0))
		{
			co_return ne::Result<std::size_t, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[SendFile]"));
		}

		co_return ne::Result<std::size_t, ne::OsError>::Ok(_size);

#elif defined(IS_POSIX)
		off_t offset = static_cast<off_t>(_offset);
		ssize_t sent = 0;
		std::size_t remaining = _size;

		while (remaining > 0)
		{
			const ssize_t bytes = ::sendfile(_sockFd, _fileFd, &offset, remaining);
			if (bytes > 0)
			{
				sent += bytes;
				remaining -= static_cast<std::size_t>(bytes);
			}
			else if (bytes == 0)
			{
				break;
			}
			else
			{
				co_return ne::Result<std::size_t, ne::OsError>::Error(
					ne::OsError{ ne::LastOsError() }.Context("[SendFile]"));
			}
		}

		co_return ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(sent));

#else
		co_return ne::Result<std::size_t, ne::OsError>::Error(
			ne::OsError{ 0, "SendFile not supported on this platform" });
#endif
	}
END_NS
