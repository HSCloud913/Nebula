//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <coroutine>
#include <thread>
#include <atomic>
#include <cstddef>
#include "Result.h"
#include "Error.h"
#include "Type.h"

BEGIN_NS(ne::io)
    // IOCP 완료 컨텍스트. OVERLAPPED 가 반드시 첫 번째 멤버여야 함.
    // GQCS 완료 시 overlapped 포인터를 reinterpret_cast 로 복원.
    struct FileIocpCtx
    {
        OVERLAPPED overlapped{};   // MUST BE FIRST — reinterpret_cast 복원용
        std::coroutine_handle<> handle;
        ne::Result<std::size_t, ne::OsError> result{ ne::Result<std::size_t, ne::OsError>::Ok(0) };
    };

    // Windows IOCP 기반 파일 비동기 엔진.
    // 내부 전용 스레드가 GQCS 를 구동해 완료된 파일 I/O 를 코루틴으로 전달한다.
    // 소켓용 IocpEngine 과 별도 완료 포트를 유지 (Io 모듈과 Network 모듈의 분리).
    class FileIocpEngine final
    {
        NEBULA_NON_COPYABLE_MOVABLE(FileIocpEngine)

    public:
        explicit FileIocpEngine() noexcept;
        ~FileIocpEngine();

    private:
        HANDLE iocpHandle{ INVALID_HANDLE_VALUE };
        std::thread thread;
        std::atomic<bool> running{ false };

        void ThreadLoop();

    public:
        [[nodiscard]] bool_t IsValid() const noexcept { return iocpHandle != INVALID_HANDLE_VALUE; }

        // AsyncFile::Create / Open 에서 파일 핸들을 이 IOCP 에 등록
        [[nodiscard]] ne::Result<void, ne::OsError>
            RegisterFile(HANDLE _file) noexcept;

        [[nodiscard]] ne::Result<void, ne::OsError>
            SubmitRead(HANDLE _fd, void* _buf, std::size_t _len, std::size_t _offset, FileIocpCtx* _ctx) noexcept;

        [[nodiscard]] ne::Result<void, ne::OsError>
            SubmitWrite(HANDLE _fd, const void* _buf, std::size_t _len, std::size_t _offset, FileIocpCtx* _ctx) noexcept;
    };
END_NS

#endif // _WIN32
