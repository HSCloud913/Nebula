//
// Created by hscloud on 26. 6. 30.
//

#if defined(_WIN32)

#include "IocpEngine.h"

BEGIN_NS(ne::io)
    FileIocpEngine::FileIocpEngine() noexcept
        : iocpHandle(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1))
    {
        if (iocpHandle == nullptr)
        {
            iocpHandle = INVALID_HANDLE_VALUE;
            return;
        }

        running.store(true, std::memory_order_relaxed);
        thread = std::thread(&FileIocpEngine::ThreadLoop, this);
    }

    FileIocpEngine::~FileIocpEngine()
    {
        if (!IsValid()) return;

        running.store(false, std::memory_order_release);
        // nullptr OVERLAPPED 로 GQCS 를 깨워 스레드 종료
        (void)::PostQueuedCompletionStatus(iocpHandle, 0, 0, nullptr);

        if (thread.joinable()) thread.join();
        ::CloseHandle(iocpHandle);
    }



    void FileIocpEngine::ThreadLoop()
    {
        while (running.load(std::memory_order_acquire))
        {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED* ov = nullptr;

            const BOOL ok = ::GetQueuedCompletionStatus(iocpHandle, &bytes, &key, &ov, 100);

            if (!ov)
                continue; // timeout 또는 shutdown 신호 (nullptr OVERLAPPED)

            auto* ctx = reinterpret_cast<FileIocpCtx*>(ov);

            if (!ok)
                ctx->result = ne::Result<std::size_t, ne::OsError>::Error(
                    ne::OsError{ ne::LastOsError() }.Context("[FileIocpEngine]"));
            else
                ctx->result = ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(bytes));

            ctx->handle.resume();
        }
    }



    ne::Result<void, ne::OsError> FileIocpEngine::RegisterFile(const HANDLE _file) noexcept
    {
        const HANDLE assoc = ::CreateIoCompletionPort(_file, iocpHandle, 0, 0);
        if (!assoc)
            return ne::Result<void, ne::OsError>::Error(
                ne::OsError{ ne::LastOsError() }.Context("[FileIocpEngine/RegisterFile]"));

        return ne::Result<void, ne::OsError>::Ok();
    }

    ne::Result<void, ne::OsError> FileIocpEngine::SubmitRead(
        const HANDLE _fd, void* _buf, const std::size_t _len, const std::size_t _offset,
        FileIocpCtx* _ctx) noexcept
    {
        _ctx->overlapped = {};
        _ctx->overlapped.Offset     = static_cast<DWORD>(_offset & 0xFFFFFFFF);
        _ctx->overlapped.OffsetHigh = static_cast<DWORD>(_offset >> 32);

        if (!::ReadFile(_fd, _buf, static_cast<DWORD>(_len), nullptr, &_ctx->overlapped))
        {
            const DWORD err = ::GetLastError();
            if (err != ERROR_IO_PENDING)
                return ne::Result<void, ne::OsError>::Error(
                    ne::OsError{ static_cast<ne::ulong_t>(err) }.Context("[FileIocpEngine/SubmitRead]"));
        }

        return ne::Result<void, ne::OsError>::Ok();
    }

    ne::Result<void, ne::OsError> FileIocpEngine::SubmitWrite(
        const HANDLE _fd, const void* _buf, const std::size_t _len, const std::size_t _offset,
        FileIocpCtx* _ctx) noexcept
    {
        _ctx->overlapped = {};
        _ctx->overlapped.Offset     = static_cast<DWORD>(_offset & 0xFFFFFFFF);
        _ctx->overlapped.OffsetHigh = static_cast<DWORD>(_offset >> 32);

        if (!::WriteFile(_fd, _buf, static_cast<DWORD>(_len), nullptr, &_ctx->overlapped))
        {
            const DWORD err = ::GetLastError();
            if (err != ERROR_IO_PENDING)
                return ne::Result<void, ne::OsError>::Error(
                    ne::OsError{ static_cast<ne::ulong_t>(err) }.Context("[FileIocpEngine/SubmitWrite]"));
        }

        return ne::Result<void, ne::OsError>::Ok();
    }
END_NS

#endif // _WIN32
