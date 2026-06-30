//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#include <coroutine>
#include <span>
#include <cstddef>
#include "Result.h"
#include "Error.h"
#include "Type.h"
#include "IoType.h"

#if defined(IS_POSIX)
#   include "IoUring/IoUringEngine.h"

BEGIN_NS(ne::io)
    class FileReadAwaitable
    {
    public:
        FileReadAwaitable(IoUringEngine& _engine, file_t _fd,
                          std::span<ne::byte_t> _buf, std::size_t _offset) noexcept
            : engine(_engine), fd(_fd), buf(_buf), offset(_offset) {}

    private:
        IoUringEngine& engine;
        file_t fd;
        std::span<ne::byte_t> buf;
        std::size_t offset;
        FileIoCtx ctx{};

    public:
        [[nodiscard]] bool_t await_ready() const noexcept { return false; }

        bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
        {
            ctx.handle = _handle;
            auto r = engine.SubmitRead(fd, buf.data(), buf.size(), offset, &ctx);
            if (r.IsError())
            {
                ctx.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
                return false; // 즉시 재개, await_resume 에서 에러 반환
            }
            return true;
        }

        [[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept
        {
            return std::move(ctx.result);
        }
    };

    class FileWriteAwaitable
    {
    public:
        FileWriteAwaitable(IoUringEngine& _engine, file_t _fd,
                           std::span<const ne::byte_t> _buf, std::size_t _offset) noexcept
            : engine(_engine), fd(_fd), buf(_buf), offset(_offset) {}

    private:
        IoUringEngine& engine;
        file_t fd;
        std::span<const ne::byte_t> buf;
        std::size_t offset;
        FileIoCtx ctx{};

    public:
        [[nodiscard]] bool_t await_ready() const noexcept { return false; }

        bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
        {
            ctx.handle = _handle;
            auto r = engine.SubmitWrite(fd, buf.data(), buf.size(), offset, &ctx);
            if (r.IsError())
            {
                ctx.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
                return false;
            }
            return true;
        }

        [[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept
        {
            return std::move(ctx.result);
        }
    };
END_NS

#elif defined(_WIN32)
#   include "Iocp/IocpEngine.h"

BEGIN_NS(ne::io)
    class FileReadAwaitable
    {
    public:
        FileReadAwaitable(FileIocpEngine& _engine, file_t _fd,
                          std::span<ne::byte_t> _buf, std::size_t _offset) noexcept
            : engine(_engine), fd(_fd), buf(_buf), offset(_offset) {}

    private:
        FileIocpEngine& engine;
        file_t fd;
        std::span<ne::byte_t> buf;
        std::size_t offset;
        FileIocpCtx ctx{};

    public:
        [[nodiscard]] bool_t await_ready() const noexcept { return false; }

        bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
        {
            ctx.handle = _handle;
            auto r = engine.SubmitRead(fd, buf.data(), buf.size(), offset, &ctx);
            if (r.IsError())
            {
                ctx.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
                return false;
            }
            return true;
        }

        [[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept
        {
            return std::move(ctx.result);
        }
    };

    class FileWriteAwaitable
    {
    public:
        FileWriteAwaitable(FileIocpEngine& _engine, file_t _fd,
                           std::span<const ne::byte_t> _buf, std::size_t _offset) noexcept
            : engine(_engine), fd(_fd), buf(_buf), offset(_offset) {}

    private:
        FileIocpEngine& engine;
        file_t fd;
        std::span<const ne::byte_t> buf;
        std::size_t offset;
        FileIocpCtx ctx{};

    public:
        [[nodiscard]] bool_t await_ready() const noexcept { return false; }

        bool_t await_suspend(std::coroutine_handle<> _handle) noexcept
        {
            ctx.handle = _handle;
            auto r = engine.SubmitWrite(fd, buf.data(), buf.size(), offset, &ctx);
            if (r.IsError())
            {
                ctx.result = ne::Result<std::size_t, ne::OsError>::Error(std::move(r.Error()));
                return false;
            }
            return true;
        }

        [[nodiscard]] ne::Result<std::size_t, ne::OsError> await_resume() noexcept
        {
            return std::move(ctx.result);
        }
    };
END_NS

#endif // platform
