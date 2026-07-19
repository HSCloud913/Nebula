//
// Created by hscloud on 26. 7. 8.
//

#include "Io/File/File.h"

#include <utility>
#include "Io/Context/Context.h"
#include "Io/Coroutine/Awaitable.h"
#include "Base/Error.h"

#if defined(IS_POSIX)
#   include <fcntl.h>
#   include <unistd.h>
#endif



namespace
{
	[[nodiscard]] ne::ulonglong_t ToHandleValue(const ne::io::file_t _handle) noexcept
	{
#if defined(_WIN32)
		return reinterpret_cast<ne::ulonglong_t>(_handle);
#elif defined(IS_POSIX)
		return static_cast<ne::ulonglong_t>(_handle);
#endif
	}
}



BEGIN_NS(ne::io)
	File::File(FileHandle&& _handle, Context& _context) noexcept
		: handle(std::move(_handle))
		, context(&_context) {}



	IoResult<File> File::Open(Context& _context, const string_view_t _path, const OpenMode _mode)
	{
		const string_t path{ _path };

#if defined(_WIN32)
		ulong_t access = 0;
		ulong_t disposition = 0;
		switch (_mode)
		{
			case OpenMode::READ:
				access = GENERIC_READ;
				disposition = OPEN_EXISTING;
				break;
			case OpenMode::WRITE:
				access = GENERIC_WRITE;
				disposition = CREATE_ALWAYS;
				break;
			case OpenMode::READ_WRITE:
				access = GENERIC_READ | GENERIC_WRITE;
				disposition = OPEN_ALWAYS;
				break;
		}

		const file_t raw = ::CreateFileA(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, disposition, FILE_FLAG_OVERLAPPED, nullptr);
		if (raw == INVALID_HANDLE_VALUE)
			return IoResult<File>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[File/Open]"));

		return IoResult<File>::Ok(File{ FileHandle{ raw }, _context });
#elif defined(IS_POSIX)
		int_t flags = 0;
		switch (_mode)
		{
			case OpenMode::READ:
				flags = O_RDONLY;
				break;
			case OpenMode::WRITE:
				flags = O_WRONLY | O_CREAT | O_TRUNC;
				break;
			case OpenMode::READ_WRITE:
				flags = O_RDWR | O_CREAT;
				break;
		}

		const file_t raw = ::open(path.c_str(), flags, 0644);
		if (raw < 0)
			return IoResult<File>::Error(IoError{ ne::OsError{ ne::LastOsError() } }.Context("[File/Open]"));

		return IoResult<File>::Ok(File{ FileHandle{ raw }, _context });
#endif
	}

	ne::Result<void_t, IoError> File::Close()
	{
		handle = FileHandle{};
		return ne::Result<void_t, IoError>::Ok();
	}



	ne::Task<IoResult<std::size_t>> File::Read(std::span<ne::byte_t> _buffer, const ulonglong_t _offset, std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::READ, .handle = ToHandleValue(handle.Get()), .buffer = _buffer.data(), .length = _buffer.size(), .offset = _offset };

		co_return co_await Awaitable{ *context, request, std::move(_stopToken) };
	}

	ne::Task<IoResult<std::size_t>> File::Write(std::span<const ne::byte_t> _buffer, const ulonglong_t _offset, std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::WRITE, .handle = ToHandleValue(handle.Get()), .buffer = const_cast<ne::byte_t*>(_buffer.data()), .length = _buffer.size(), .offset = _offset };

		co_return co_await Awaitable{ *context, request, std::move(_stopToken) };
	}


	ne::Task<IoResult<std::size_t>> File::Readv(const BufferChain& _chain, const ulonglong_t _offset, std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::READ, .handle = ToHandleValue(handle.Get()), .length = _chain.TotalSize(), .offset = _offset, .chain = &_chain };

		co_return co_await Awaitable{ *context, request, std::move(_stopToken) };
	}

	ne::Task<IoResult<std::size_t>> File::Writev(const BufferChain& _chain, const ulonglong_t _offset, std::stop_token _stopToken)
	{
		const Request request{ .requestKind = RequestKind::WRITE, .handle = ToHandleValue(handle.Get()), .length = _chain.TotalSize(), .offset = _offset, .chain = &_chain };

		co_return co_await Awaitable{ *context, request, std::move(_stopToken) };
	}

END_NS
