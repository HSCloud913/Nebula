//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include <chrono>
#include <cstdint>
#include "NetworkType.h"
#include "Handle.h"
#include "Result.h"
#include "Error.h"

BEGIN_NS(ne::network)

// 소켓 RAII 래퍼.
// 역할: fd 생성/소멸, Connect/Bind/Listen/Accept, 소켓 옵션.
// 송수신 없음 — 송수신은 상위 Stream 레이어 책임.
class Socket
{
public:
    NEBULA_NON_COPYABLE(Socket)
    NEBULA_DEFAULT_MOVE(Socket)

private:
    explicit Socket(socket_t _fd);

public:
    ~Socket() = default;

private:
#if defined(_WIN32)
    using SocketHandle = ne::Handle<
        socket_t,
        decltype([](const socket_t _socket) { ::closesocket(_socket); }),
        INVALID_SOCKET
    >;
#elif defined(IS_POSIX)
    using SocketHandle = ne::Handle<
        socket_t,
        decltype([](const socket_t _socket) { ::close(_socket); }),
        static_cast<socket_t>(-1)
    >;
#endif

    SocketHandle handle;

public:
    [[nodiscard]] static ne::Result<Socket, ne::OsError> CreateTcp();
    [[nodiscard]] static ne::Result<Socket, ne::OsError> CreateUdp();

public: /* Option */
    [[nodiscard]] ne::Result<void, ne::OsError> SetReuseAddr(bool_t _enable);
    [[nodiscard]] ne::Result<void, ne::OsError> SetNoDelay(bool_t _enable);
    [[nodiscard]] ne::Result<void, ne::OsError> SetNonBlocking(bool_t _enable);
    [[nodiscard]] ne::Result<void, ne::OsError> SetRecvTimeout(std::chrono::milliseconds _timeout);
    [[nodiscard]] ne::Result<void, ne::OsError> SetSendTimeout(std::chrono::milliseconds _timeout);

public: /* Server */
    [[nodiscard]] ne::Result<void, ne::OsError> Bind(string_view_t _address, uint16_t _port);
    [[nodiscard]] ne::Result<void, ne::OsError> Listen(int_t _backlog = SOMAXCONN);
    [[nodiscard]] ne::Result<Socket, ne::OsError> Accept();

public: /* Client */
    [[nodiscard]] ne::Result<void, ne::OsError> Connect(string_view_t _address, uint16_t _port);

public:
    [[nodiscard]] socket_t Handle()  const noexcept { return handle.Get(); }
    [[nodiscard]] bool_t IsValid() const noexcept { return static_cast<bool_t>(handle); }

private:
    static ne::Result<sockaddr_in, ne::OsError> ResolveAddress(string_view_t _address, uint16_t _port);
};

END_NS
