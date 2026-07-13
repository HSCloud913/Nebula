cmake_minimum_required(VERSION 3.28)
project(NebulaNetwork VERSION 1.0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    #Socket/Socket.cpp
    Dns/Dns.cpp
    Stream/Plain/PlainStream.cpp
    Stream/Tls/TlsStream.cpp
    #Stream/SSH/SshStream.cpp
    #Protocol/Http/Http1/Client.cpp
    #Protocol/Http/Http1/Server.cpp
    #Protocol/Http/Http2/Hpack.cpp
    #Protocol/Http/Http2/Stream.cpp
    #Protocol/Http/Http2/FlowControl.cpp
    #Protocol/Http/Http2/Client.cpp
    #Protocol/Http/Http2/Server.cpp
    #Protocol/Ftp/Client.cpp
    #Protocol/Sftp/Client.cpp
)

if (WIN32)
	list(APPEND SOURCES
	     Stream/Tls/Schannel/TlsMessageBuffer.cpp
	)
endif ()

add_library(NebulaNetwork STATIC ${SOURCES})

target_include_directories(NebulaNetwork PUBLIC
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           ${CMAKE_SOURCE_DIR}/Base
)

target_link_libraries(NebulaNetwork PUBLIC
                      NebulaBase
                      NebulaMemory
                      NebulaConcurrency
                      NebulaTime
                      NebulaUtil
                      NebulaIo
)

if (WIN32)
	target_link_libraries(NebulaNetwork PRIVATE Ws2_32 Secur32 Crypt32 Mswsock)
endif ()

if (UNIX)
	find_package(OpenSSL QUIET)
	if (TARGET OpenSSL::SSL)
		target_compile_definitions(NebulaNetwork PRIVATE NEBULA_WITH_OPENSSL)
		target_link_libraries(NebulaNetwork PRIVATE OpenSSL::SSL OpenSSL::Crypto)
	endif ()
endif ()

if (TARGET libssh2::libssh2)
	target_compile_definitions(NebulaNetwork PRIVATE NEBULA_WITH_LIBSSH2)
	target_link_libraries(NebulaNetwork PRIVATE libssh2::libssh2)
endif ()


if (TARGET GTest::gtest_main AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Test)
	add_subdirectory(Test)
endif ()