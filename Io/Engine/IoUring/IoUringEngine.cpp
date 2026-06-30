//
// Created by hscloud on 26. 6. 30.
//

#if defined(IS_POSIX)

#include "IoUringEngine.h"
#include <cerrno>
#include <cstring>

BEGIN_NS(ne::io)
    IoUringEngine::IoUringEngine(const unsigned _queueDepth) noexcept
    {
        if (::io_uring_queue_init(_queueDepth, &ring, 0) < 0)
            return;

        valid = true;
        running.store(true, std::memory_order_relaxed);
        thread = std::thread(&IoUringEngine::ThreadLoop, this);
    }

    IoUringEngine::~IoUringEngine()
    {
        if (!valid) return;

        running.store(false, std::memory_order_release);

        // NOP SQE 제출로 io_uring_wait_cqe_timeout 을 깨움
        if (auto* sqe = ::io_uring_get_sqe(&ring))
        {
            ::io_uring_prep_nop(sqe);
            ::io_uring_sqe_set_data(sqe, nullptr);
            (void)::io_uring_submit(&ring);
        }

        if (thread.joinable()) thread.join();
        ::io_uring_queue_exit(&ring);
    }



    void IoUringEngine::ThreadLoop()
    {
        while (running.load(std::memory_order_acquire))
            (void)ProcessCompletions(100);
    }



    ne::Result<void, ne::OsError> IoUringEngine::SubmitRead(
        const int _fd, void* _buf, const std::size_t _len, const std::size_t _offset,
        FileIoCtx* _ctx) noexcept
    {
        auto* sqe = ::io_uring_get_sqe(&ring);
        if (!sqe)
            return ne::Result<void, ne::OsError>::Error(ne::OsError{ 0, "io_uring SQ full" });

        ::io_uring_prep_read(sqe, _fd, _buf, static_cast<unsigned>(_len), static_cast<__u64>(_offset));
        ::io_uring_sqe_set_data(sqe, _ctx);

        if (::io_uring_submit(&ring) < 0)
            return ne::Result<void, ne::OsError>::Error(
                ne::OsError{ static_cast<ne::ulong_t>(errno) }.Context("[IoUringEngine/SubmitRead]"));

        return ne::Result<void, ne::OsError>::Ok();
    }

    ne::Result<void, ne::OsError> IoUringEngine::SubmitWrite(
        const int _fd, const void* _buf, const std::size_t _len, const std::size_t _offset,
        FileIoCtx* _ctx) noexcept
    {
        auto* sqe = ::io_uring_get_sqe(&ring);
        if (!sqe)
            return ne::Result<void, ne::OsError>::Error(ne::OsError{ 0, "io_uring SQ full" });

        ::io_uring_prep_write(sqe, _fd, _buf, static_cast<unsigned>(_len), static_cast<__u64>(_offset));
        ::io_uring_sqe_set_data(sqe, _ctx);

        if (::io_uring_submit(&ring) < 0)
            return ne::Result<void, ne::OsError>::Error(
                ne::OsError{ static_cast<ne::ulong_t>(errno) }.Context("[IoUringEngine/SubmitWrite]"));

        return ne::Result<void, ne::OsError>::Ok();
    }

    ne::Result<void, ne::OsError> IoUringEngine::ProcessCompletions(const ne::int_t _timeoutMs) noexcept
    {
        io_uring_cqe* cqe = nullptr;

        int ret;
        if (_timeoutMs < 0)
        {
            ret = ::io_uring_wait_cqe(&ring, &cqe);
        }
        else
        {
            __kernel_timespec ts{};
            ts.tv_sec  = _timeoutMs / 1000;
            ts.tv_nsec = static_cast<long long>(_timeoutMs % 1000) * 1'000'000LL;
            ret = ::io_uring_wait_cqe_timeout(&ring, &cqe, &ts);
        }

        if (ret < 0 || !cqe)
            return ne::Result<void, ne::OsError>::Ok(); // timeout or EINTR — not fatal

        while (cqe)
        {
            auto* ctx  = reinterpret_cast<FileIoCtx*>(::io_uring_cqe_get_data(cqe));
            const int res = cqe->res;
            ::io_uring_cqe_seen(&ring, cqe);

            if (ctx) // nullptr → NOP (shutdown signal)
            {
                if (res < 0)
                    ctx->result = ne::Result<std::size_t, ne::OsError>::Error(
                        ne::OsError{ static_cast<ne::ulong_t>(-res) }.Context("[IoUringEngine/ProcessCompletions]"));
                else
                    ctx->result = ne::Result<std::size_t, ne::OsError>::Ok(static_cast<std::size_t>(res));

                ctx->handle.resume();
            }

            cqe = nullptr;
            (void)::io_uring_peek_cqe(&ring, &cqe);
        }

        return ne::Result<void, ne::OsError>::Ok();
    }
END_NS

#endif // IS_POSIX
