//
// Created by hscloud on 26. 6. 30.
//

#include "AsyncFile.h"
#include <utility>

#if defined(_WIN32)
#   include <windows.h>
#elif defined(IS_POSIX)
#   include <fcntl.h>
#   include <unistd.h>
#   include <sys/types.h>
#endif

BEGIN_NS(ne::io)
	AsyncFile::AsyncFile(const file_t _fd) noexcept
		: fd(_fd) {}

	AsyncFile::AsyncFile(AsyncFile&& _other) noexcept
		: fd(std::exchange(_other.fd, InvalidFile)) {}

	AsyncFile& AsyncFile::operator=(AsyncFile&& _other) noexcept
	{
		if (this != &_other)
		{
			(void)Close();
			fd = std::exchange(_other.fd, InvalidFile);
		}
		return *this;
	}

	AsyncFile::~AsyncFile()
	{
		(void)Close();
	}



	ne::Result<AsyncFile, ne::OsError> AsyncFile::Create(const ne::string_t& _path) noexcept
	{
#if defined(_WIN32)
		const HANDLE handle = ::CreateFileA(_path.c_str(),GENERIC_READ | GENERIC_WRITE, 0, nullptr,CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE)
			return ne::Result<AsyncFile, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[AsyncFile/Create]"));

		return ne::Result<AsyncFile, ne::OsError>::Ok(AsyncFile{ handle });
#elif defined(IS_POSIX)
		const int fd = ::open(_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
		if (fd < 0)
			return ne::Result<AsyncFile, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[AsyncFile/Create]"));

		return ne::Result<AsyncFile, ne::OsError>::Ok(AsyncFile{ fd });
#else
		(void)_path;

		return ne::Result<AsyncFile, ne::OsError>::Error(
			ne::OsError{ 0, "AsyncFile not supported on this platform" });
#endif
	}

	ne::Result<AsyncFile, ne::OsError> AsyncFile::Open(const ne::string_t& _path, const bool_t _readOnly) noexcept
	{
#if defined(_WIN32)
		const HANDLE handle = ::CreateFileA(_path.c_str(), _readOnly ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE), FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE)
			return ne::Result<AsyncFile, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[AsyncFile/Open]"));
		return ne::Result<AsyncFile, ne::OsError>::Ok(AsyncFile{ handle });

#elif defined(IS_POSIX)
		const int flags = _readOnly ? O_RDONLY : O_RDWR;
		const int fd = ::open(_path.c_str(), flags);
		if (fd < 0)
			return ne::Result<AsyncFile, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[AsyncFile/Open]"));

		return ne::Result<AsyncFile, ne::OsError>::Ok(AsyncFile{ fd });
#else
		(void)_path;
		(void)_readOnly;

		return ne::Result<AsyncFile, ne::OsError>::Error(
			ne::OsError{ 0, "AsyncFile not supported on this platform" });
#endif
	}



	ne::Task<ne::Result<std::size_t, ne::OsError>> AsyncFile::Read(std::span<ne::byte_t> _buf, const std::size_t _offset)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "file not open" });

#if defined(_WIN32)
		OVERLAPPED ov{};
		ov.Offset = static_cast<ulong_t>(_offset & 0xFFFFFFFF);
		ov.OffsetHigh = static_cast<ulong_t>(_offset >> 32);

		ulong_t bytes = 0;
		if (!::ReadFile(fd, _buf.data(), _buf.size(), &bytes, &ov))
		{
			if (const auto error = LastOsError(); error != ERROR_HANDLE_EOF)
				co_return ne::Result<std::size_t, ne::OsError>::Error(
					ne::OsError{ static_cast<ne::ulong_t>(error) }.Context("[AsyncFile/Read]"));
		}

		co_return ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(bytes));
#elif defined(IS_POSIX)
		const ssize_t bytes = ::pread(fd, _buf.data(), _buf.size(), static_cast<off_t>(_offset));
		if (bytes < 0)
			co_return ne::Result<std::size_t, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[AsyncFile/Read]"));

		co_return ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(bytes));
#else
		co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "not supported" });
#endif
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> AsyncFile::Write(std::span<const ne::byte_t> _data, const std::size_t _offset)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "file not open" });

#if defined(_WIN32)
		OVERLAPPED ov{};
		ov.Offset = static_cast<ulong_t>(_offset & 0xFFFFFFFF);
		ov.OffsetHigh = static_cast<ulong_t>(_offset >> 32);

		ulong_t bytes = 0;
		if (!::WriteFile(fd, _data.data(), _data.size(), &bytes, &ov))
			co_return ne::Result<std::size_t, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[AsyncFile/Write]"));

		co_return ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(bytes));
#elif defined(IS_POSIX)
		const ssize_t bytes = ::pwrite(fd, _data.data(), _data.size(), static_cast<off_t>(_offset));
		if (bytes < 0)
			co_return ne::Result<std::size_t, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[AsyncFile/Write]"));

		co_return ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(bytes));
#else
		co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "not supported" });
#endif
	}

	ne::Result<void, ne::OsError> AsyncFile::Close()
	{
		if (!IsOpen()) return ne::Result<void, ne::OsError>::Ok();

#if defined(_WIN32)
		::CloseHandle(fd);
		fd = INVALID_HANDLE_VALUE;
#elif defined(IS_POSIX)
		::close(fd);
		fd = -1;
#endif
		return ne::Result<void, ne::OsError>::Ok();
	}

END_NS
