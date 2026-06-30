cmake_minimum_required(VERSION 3.28)
project(NebulaNetwork VERSION 1.0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BASE_DIR "${CMAKE_SOURCE_DIR}/Base")

set(SOURCES
    Socket/Socket.cpp
    Stream/PlainStream.cpp
    Stream/TlsStream.cpp
    Stream/SshStream.cpp
    Http/Http1/Client.cpp
    Http/Http1/Server.cpp
    #Http/Http2/Hpack.cpp
    #Http/Http2/Stream.cpp
    #Http/Http2/FlowControl.cpp
    #Http/Http2/Client.cpp
    #Http/Http2/Server.cpp
    Buffer/RawBuffer.cpp
        Ftp/Client.cpp
        Sftp/Client.cpp
)

if (UNIX)
    list(APPEND SOURCES IoEngine/EpollEngine.cpp)
endif()

if (WIN32)
    list(APPEND SOURCES
        IoEngine/IocpEngine.cpp
        Stream/Schannel/TlsMessageBuffer.cpp
    )
endif()

add_library(NebulaNetwork STATIC ${SOURCES})

target_include_directories(NebulaNetwork PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}   # Network/ 루트
    ${BASE_DIR}                   # NebulaHandle.h, Result.h, Error.h, Type.h
)

target_link_libraries(NebulaNetwork PUBLIC
    NebulaBase
    NebulaMemory
    NebulaConcurrency
    NebulaTime
    NebulaIo
)

# ── Windows: SChannel ────────────────────────────────────────
if (WIN32)
    target_link_libraries(NebulaNetwork PRIVATE Ws2_32 Secur32 Crypt32)
endif()

# ── POSIX: OpenSSL (TLS) ─────────────────────────────────────
if (UNIX)
    find_package(OpenSSL QUIET)
    if (TARGET OpenSSL::SSL)
        target_compile_definitions(NebulaNetwork PRIVATE NEBULA_WITH_OPENSSL)
        target_link_libraries(NebulaNetwork PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()
endif()

# ── libssh2: 루트 CMakeLists.txt 의 FetchContent 로 이미 포함됨 ──
if (TARGET libssh2::libssh2)
    target_compile_definitions(NebulaNetwork PRIVATE NEBULA_WITH_LIBSSH2)
    target_link_libraries(NebulaNetwork PRIVATE libssh2::libssh2)
endif()

add_subdirectory(Test)
