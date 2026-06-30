//
// Created by hscloud on 26. 6. 30.
//

#pragma once
#if defined(IS_POSIX)

#include <coroutine>
#include <thread>
#include <atomic>
#include <cstddef>
#include <liburing.h>
#include "Result.h"
#include "Error.h"
#include "Type.h"

BEGIN_NS(ne::io)
    // io_uring 완료 컨텍스트. FileReadAwaitable / FileWriteAwaitable 이 소유.
    // io_uring user_data 로 포인터를 전달하고 CQE 처리 시 복원.
    struct FileIoCtx
    {
        std::coroutine_handle<> handle;
        ne::Result<std::size_t, ne::OsError> result{ ne::Result<std::size_t, ne::OsError>::Ok(0) };
    };

    // Linux io_uring 기반 파일 비동기 엔진.
    // 내부 전용 스레드가 CQE를 처리하고 대기 중인 코루틴을 재개한다.
    class IoUringEngine final
    {
        NEBULA_NON_COPYABLE_MOVABLE(IoUringEngine)

    public:
        explicit IoUringEngine(unsigned _queueDepth = 256) noexcept;
        ~IoUringEngine();

    private:
        io_uring ring{};
        std::thread thread;
        std::atomic<bool> running{ false };
        bool valid{ false };

        void ThreadLoop();

    public:
        [[nodiscard]] bool_t IsValid() const noexcept { return valid; }

        [[nodiscard]] ne::Result<void, ne::OsError>
            SubmitRead(int _fd, void* _buf, std::size_t _len, std::size_t _offset, FileIoCtx* _ctx) noexcept;

        [[nodiscard]] ne::Result<void, ne::OsError>
            SubmitWrite(int _fd, const void* _buf, std::size_t _len, std::size_t _offset, FileIoCtx* _ctx) noexcept;

        // 완료된 CQE를 처리. -1이면 CQE 도착까지 무기한 대기.
        [[nodiscard]] ne::Result<void, ne::OsError>
            ProcessCompletions(ne::int_t _timeoutMs = -1) noexcept;
    };
END_NS

#endif // IS_POSIX
