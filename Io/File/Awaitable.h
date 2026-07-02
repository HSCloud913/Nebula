//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <coroutine>
#include <span>
#include <cstddef>
#include "Engine/IIoEngine.h"
#include "IoType.h"
#include "Result.h"
#include "Error.h"
#include "Type.h"

#if defined(IS_POSIX)
#   include "Engine/IoUring/IoUringEngine.h"
#elif defined(_WIN32)
#   include "Engine/Iocp/IocpEngine.h"
#endif

BEGIN_NS(ne::io)
	// ── 파일 I/O Awaitable ────────────────────────────────────────────────────
	// SubmitRead/SubmitWrite 는 IIoEngine 외부 메서드이므로 구체 엔진 타입을 참조.
	// context 타입은 IoContext 로 통일 (Windows: OVERLAPPED 포함, Linux: 미포함).

#if defined(_WIN32)
	// 파일 읽기 비동기 대기 (IOCP).
	// co_await → Result<size_t, OsError>
	class FileReadAwaitable
	{
	public:
		FileReadAwaitable(IocpEngine& _engine, const file_t _fd, const std::span<ne::byte_t> _buffer, const std::size_t _offset) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, offset(_offset) {}

	private:
		IocpEngine& engine;
		file_t fd;
		std::span<ne::byte_t> buffer;
		std::size_t offset;
		IoContext context{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			context.handle = _handle;

			auto result = engine.SubmitRead(fd, buffer.data(), buffer.size(), offset, &context);
			if (result.IsError())
			{
				context.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return std::move(context.result); }
	};

	// 파일 쓰기 비동기 대기 (IOCP).
	// co_await → Result<size_t, OsError>
	class FileWriteAwaitable
	{
	public:
		FileWriteAwaitable(IocpEngine& _engine, const file_t _fd, const std::span<const ne::byte_t> _buffer, const std::size_t _offset) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, offset(_offset) {}

	private:
		IocpEngine& engine;
		file_t fd;
		std::span<const ne::byte_t> buffer;
		std::size_t offset;
		IoContext context{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			context.handle = _handle;

			if (auto result = engine.SubmitWrite(fd, buffer.data(), buffer.size(), offset, &context); result.IsError())
			{
				context.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return std::move(context.result); }
	};
#elif defined(IS_POSIX)

	// 파일 읽기 비동기 대기 (io_uring).
	// co_await → Result<size_t, OsError>
	class FileReadAwaitable
	{
	public:
		FileReadAwaitable(IoUringEngine& _engine, const file_t _fd, const std::span<ne::byte_t> _buffer, const std::size_t _offset) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, offset(_offset) {}

	private:
		IoUringEngine& engine;
		file_t fd;
		std::span<ne::byte_t> buffer;
		std::size_t offset;
		IoContext context{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			context.handle = _handle;

			if (auto result = engine.SubmitRead(fd, buffer.data(), buffer.size(), offset, &context); result.IsError())
			{
				context.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return std::move(context.result); }
	};

	// 파일 쓰기 비동기 대기 (io_uring).
	// co_await → Result<size_t, OsError>
	class FileWriteAwaitable
	{
	public:
		FileWriteAwaitable(IoUringEngine& _engine, const file_t _fd, const std::span<const ne::byte_t> _buffer, const std::size_t _offset) noexcept
			: engine(_engine)
			, fd(_fd)
			, buffer(_buffer)
			, offset(_offset) {}

	private:
		IoUringEngine& engine;
		file_t fd;
		std::span<const ne::byte_t> buffer;
		std::size_t offset;
		IoContext context{};

	public:
		[[nodiscard]] bool_t await_ready() const noexcept { return false; }

		bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
		{
			context.handle = _handle;

			if (auto result = engine.SubmitWrite(fd, buffer.data(), buffer.size(), offset, &context); result.IsError())
			{
				context.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(result.Error()));
				return false;
			}

			return true;
		}

		[[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept { return std::move(context.result); }
	};



#endif // platform

END_NS
