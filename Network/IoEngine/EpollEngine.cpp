//
// Created by hscloud on 25. 6. 29.
//

#if defined(IS_POSIX)
#include "EpollEngine.h"
#include <cerrno>
#include <sys/epoll.h>

BEGIN_NS(ne::network)
    static constexpr int MaxEvents = 64;



    EpollEngine::EpollEngine()
        : epollFd(::epoll_create1(EPOLL_CLOEXEC)) {}



    ne::Result<void, ne::OsError> EpollEngine::Watch(const socket_t _fd, const uint32_t _events, IoCallback _cb)
    {
        epoll_event ev{};
        ev.events  = ToEpollEvents(_events);
        ev.data.fd = _fd;

        const bool_t alreadyWatched = watches.contains(_fd);
        const int    op             = alreadyWatched ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;

        if (::epoll_ctl(epollFd.Get(), op, _fd, &ev) == -1)
            return ne::Result<void, ne::OsError>::Error(ne::OsError{errno}.Context("[EpollEngine/Watch]"));

        watches[_fd] = { _events, std::move(_cb) };
        return ne::Result<void, ne::OsError>::Ok();
    }

    ne::Result<void, ne::OsError> EpollEngine::Unwatch(const socket_t _fd)
    {
        if (!watches.contains(_fd))
            return ne::Result<void, ne::OsError>::Ok();

        if (::epoll_ctl(epollFd.Get(), EPOLL_CTL_DEL, _fd, nullptr) == -1)
            return ne::Result<void, ne::OsError>::Error(ne::OsError{errno}.Context("[EpollEngine/Unwatch]"));

        watches.erase(_fd);
        return ne::Result<void, ne::OsError>::Ok();
    }

    ne::Result<void, ne::OsError> EpollEngine::RunOnce(const int_t _timeoutMs)
    {
        epoll_event events[MaxEvents];
        const int count = ::epoll_wait(epollFd.Get(), events, MaxEvents, _timeoutMs);

        if (count == -1)
        {
            if (errno == EINTR)
                return ne::Result<void, ne::OsError>::Ok();  // 시그널에 의한 인터럽트는 정상
            return ne::Result<void, ne::OsError>::Error(ne::OsError{errno}.Context("[EpollEngine/RunOnce]"));
        }

        for (int i = 0; i < count; ++i)
        {
            const auto fd = static_cast<socket_t>(events[i].data.fd);
            const auto evts = FromEpollEvents(events[i].events);

            // 콜백 내부에서 Unwatch 가 호출될 수 있으므로 반복마다 재검색.
            if (const auto watch = watches.find(fd); watch != watches.end())
                watch->second.callback(fd, evts);
        }

        return ne::Result<void, ne::OsError>::Ok();
    }

    uint32_t EpollEngine::ToEpollEvents(uint32_t _events) noexcept
    {
        uint32_t result = 0;
        if (_events & IoEvent::Read)   result |= static_cast<uint32_t>(EPOLLIN);
        if (_events & IoEvent::Write)  result |= static_cast<uint32_t>(EPOLLOUT);
        if (_events & IoEvent::Error)  result |= static_cast<uint32_t>(EPOLLERR);
        if (_events & IoEvent::HangUp) result |= static_cast<uint32_t>(EPOLLHUP);
        return result;
    }

    uint32_t EpollEngine::FromEpollEvents(uint32_t _events) noexcept
    {
        uint32_t result = 0;
        if (_events & static_cast<uint32_t>(EPOLLIN))  result |= IoEvent::Read;
        if (_events & static_cast<uint32_t>(EPOLLOUT)) result |= IoEvent::Write;
        if (_events & static_cast<uint32_t>(EPOLLERR)) result |= IoEvent::Error;
        if (_events & static_cast<uint32_t>(EPOLLHUP)) result |= IoEvent::HangUp;
        return result;
    }

END_NS

#endif // IS_POSIX
