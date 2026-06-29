//
// Created by hscloud on 25. 6. 29.
//

#pragma once
#include "Type.h"

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#   endif
#   include <winsock2.h>
#   include <ws2tcpip.h>

    BEGIN_NS(ne::network)
        using socket_t = SOCKET;
        inline constexpr socket_t InvalidSocket = INVALID_SOCKET;
    END_NS

#elif defined(IS_POSIX)
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <arpa/inet.h>
#   include <netdb.h>
#   include <unistd.h>
#   include <fcntl.h>

    BEGIN_NS(ne::network)
        using socket_t = int_t;
        inline constexpr socket_t InvalidSocket = -1;
    END_NS

#endif
