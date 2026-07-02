//
// Created by hscloud on 26. 6. 30.
//

#include "AsyncFile.h"
#include "Awaitable.h"
#include <utility>

#if defined(_WIN32)
#   include <windows.h>
#elif defined(IS_POSIX)
#   include <fcntl.h>
#   include <unistd.h>
#   include <sys/types.h>
#endif



BEGIN_NS(ne::io)
#if defined(IS_POSIX)
	AsyncFile::AsyncFile(const file_t _fd, IoUringEngine& _engine) noexcept
		: fd(_fd)
		, engine(&_engine) {}
#elif defined(_WIN32)
	AsyncFile::AsyncFile(const file_t _fd, IocpEngine& _engine) noexcept
		: fd(_fd)
		, engine(&_engine) {}
#else
	AsyncFile::AsyncFile(const file_t _fd) noexcept
		: fd(_fd) {}
#endif

	AsyncFile::AsyncFile(AsyncFile&& _other) noexcept
		: fd(std::exchange(_other.fd, InvalidFile))
#if defined(IS_POSIX) || defined(_WIN32)
		, engine(_other.engine)
#endif
	{}

	AsyncFile& AsyncFile::operator=(AsyncFile&& _other) noexcept
	{
		if (this != &_other)
		{
			(void)Close();
			fd = std::exchange(_other.fd, InvalidFile);
#if defined(IS_POSIX) || defined(_WIN32)
			engine = _other.engine;
#endif
		}
		return *this;
	}



#if defined(_WIN32)
	ne::Result<AsyncFile, ne::OsError> AsyncFile::Create(const ne::string_t& _path, IocpEngine& _engine) noexcept
	{
		const HANDLE handle = ::CreateFileA(_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
		if (handle == INVALID_HANDLE_VALUE)
			return ne::Result<AsyncFile, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[AsyncFile/Create]"));

		// RegisterFile 이 실패하면 handle 을 직접 닫고 에러 반환.
		// AsyncFile 생성은 IOCP 등록 성공 후에만 수행 (이중 CloseHandle 방지).
		if (auto result = _engine.RegisterFileHandle(handle); result.IsError())
		{
			::CloseHandle(handle);
			return ne::Result<AsyncFile, ne::OsError>::Error(std::move(result.Error()));
		}

		return ne::Result<AsyncFile, ne::OsError>::Ok(AsyncFile{ handle, _engine });
	}

	ne::Result<AsyncFile, ne::OsError> AsyncFile::Open(const ne::string_t& _path, IocpEngine& _engine, const bool_t _readOnly) noexcept
	{
		const DWORD access = _readOnly ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
		const HANDLE handle = ::CreateFileA(_path.c_str(), access, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
		if (handle == INVALID_HANDLE_VALUE)
			return ne::Result<AsyncFile, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[AsyncFile/Open]"));

		if (auto result = _engine.RegisterFileHandle(handle); result.IsError())
		{
			::CloseHandle(handle);
			return ne::Result<AsyncFile, ne::OsError>::Error(std::move(result.Error()));
		}

		return ne::Result<AsyncFile, ne::OsError>::Ok(AsyncFile{ handle, _engine });
	}

#elif defined(IS_POSIX)
	ne::Result<AsyncFile, ne::OsError> AsyncFile::Create(const ne::string_t& _path, IoUringEngine& _engine) noexcept
	{
		const int fd = ::open(_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
		if (fd < 0)
			return ne::Result<AsyncFile, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[AsyncFile/Create]"));

		return ne::Result<AsyncFile, ne::OsError>::Ok(AsyncFile{ fd, _engine });
	}

	ne::Result<AsyncFile, ne::OsError> AsyncFile::Open(const ne::string_t& _path, IoUringEngine& _engine, const bool_t _readOnly) noexcept
	{
		const int flags = _readOnly ? O_RDONLY : O_RDWR;

		const int fd = ::open(_path.c_str(), flags);
		if (fd < 0)
			return ne::Result<AsyncFile, ne::OsError>::Error(
				ne::OsError{ ne::LastOsError() }.Context("[AsyncFile/Open]"));

		return ne::Result<AsyncFile, ne::OsError>::Ok(AsyncFile{ fd, _engine });
	}

#else
	ne::Result<AsyncFile, ne::OsError> AsyncFile::Create(const ne::string_t& _path) noexcept
	{
		(void)_path;
		return ne::Result<AsyncFile, ne::OsError>::Error(
			ne::OsError{ 0, "AsyncFile not supported on this platform" });
	}

	ne::Result<AsyncFile, ne::OsError> AsyncFile::Open(const ne::string_t& _path, const bool_t _readOnly) noexcept
	{
		(void)_path;
		(void)_readOnly;
		return ne::Result<AsyncFile, ne::OsError>::Error(
			ne::OsError{ 0, "AsyncFile not supported on this platform" });
	}
#endif

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



	ne::Task<ne::Result<std::size_t, ne::OsError>> AsyncFile::Read(std::span<ne::byte_t> _buffer, const std::size_t _offset)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "file not open" });

#if defined(IS_POSIX) || defined(_WIN32)
		co_return co_await FileReadAwaitable{ *engine, fd, _buffer, _offset };
#else
		co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "not supported" });
#endif
	}

	ne::Task<ne::Result<std::size_t, ne::OsError>> AsyncFile::Write(const std::span<const ne::byte_t> _data, const std::size_t _offset)
	{
		if (!IsOpen()) co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "file not open" });

#if defined(IS_POSIX) || defined(_WIN32)
		co_return co_await FileWriteAwaitable{ *engine, fd, _data, _offset };
#else
		co_return ne::Result<std::size_t, ne::OsError>::Error(ne::OsError{ 0, "not supported" });
#endif
	}

END_NS
