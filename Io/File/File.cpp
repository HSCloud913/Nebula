//
// Created by hscloud on 26. 7. 8.
//

#include "File.h"

#include <utility>
#include "IoContext.h"
#include "Coroutine/IoAwaitable.h"
#include "Error.h"

#if defined(_WIN32)
#   include <windows.h>
#elif defined(IS_POSIX)
#   include <fcntl.h>
#   include <unistd.h>
#endif

BEGIN_NS(ne::io)
	namespace detail
	{
		void_t CloseFileHandle(const file_t _handle) noexcept
		{
#if defined(_WIN32)
			::CloseHandle(_handle);
#elif defined(IS_POSIX)
			::close(_handle);
#endif
		}
	}

	namespace
	{
		// file_t(HANDLE/int)를 IoRequest.handle(64비트)로 정규화.
		[[nodiscard]] ulonglong_t ToHandleValue(const file_t _handle) noexcept
		{
#if defined(_WIN32)
			return reinterpret_cast<ulonglong_t>(_handle);
#elif defined(IS_POSIX)
			return static_cast<ulonglong_t>(_handle);
#endif
		}
	}

	File::File(FileHandle&& _handle, IoContext& _context) noexcept
		: handle(std::move(_handle))
		, context(&_context) {}

	IoResult<File> File::Open(IoContext& _context, const string_view_t _path, const OpenMode _mode)
	{
		const string_t path{ _path };

#if defined(_WIN32)
		ulong_t access = 0;
		ulong_t disposition = 0;
		switch (_mode)
		{
		case OpenMode::Read:      access = GENERIC_READ;                  disposition = OPEN_EXISTING; break;
		case OpenMode::Write:     access = GENERIC_WRITE;                 disposition = CREATE_ALWAYS; break;
		case OpenMode::ReadWrite: access = GENERIC_READ | GENERIC_WRITE;  disposition = OPEN_ALWAYS;   break;
		}

		// FILE_FLAG_OVERLAPPED: IOCP 비동기 I/O 전제.
		const file_t raw = ::CreateFileA(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		                                 disposition, FILE_FLAG_OVERLAPPED, nullptr);
		if (raw == INVALID_HANDLE_VALUE)
			return IoResult<File>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[File/Open]"));

		return IoResult<File>::Ok(File{ FileHandle{ raw }, _context });
#elif defined(IS_POSIX)
		int_t flags = 0;
		switch (_mode)
		{
		case OpenMode::Read:      flags = O_RDONLY;                    break;
		case OpenMode::Write:     flags = O_WRONLY | O_CREAT | O_TRUNC; break;
		case OpenMode::ReadWrite: flags = O_RDWR | O_CREAT;            break;
		}

		const file_t raw = ::open(path.c_str(), flags, 0644);
		if (raw < 0)
			return IoResult<File>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[File/Open]"));

		return IoResult<File>::Ok(File{ FileHandle{ raw }, _context });
#endif
	}

	ne::Task<IoResult<std::size_t>> File::Read(std::span<ne::byte_t> _buffer, const ulonglong_t _offset)
	{
		co_return co_await IoAwaitable{ *context, IoRequest{
			.op = OpCode::Read, .handle = ToHandleValue(handle.Get()),
			.buffer = _buffer.data(), .length = _buffer.size(), .offset = _offset } };
	}

	ne::Task<IoResult<std::size_t>> File::Write(std::span<const ne::byte_t> _buffer, const ulonglong_t _offset)
	{
		co_return co_await IoAwaitable{ *context, IoRequest{
			.op = OpCode::Write, .handle = ToHandleValue(handle.Get()),
			.buffer = const_cast<ne::byte_t*>(_buffer.data()), .length = _buffer.size(), .offset = _offset } };
	}

	ne::Result<void_t, IoError> File::Close()
	{
		handle = FileHandle{}; // 기존 핸들 silently close 후 무효화
		return ne::Result<void_t, IoError>::Ok();
	}

END_NS
