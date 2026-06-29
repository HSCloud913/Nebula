cmake_minimum_required(VERSION 3.28)
project(Nebula
        VERSION 1.0.0.0)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
)
FetchContent_MakeAvailable(googletest)

if (WIN32)
    set(CRYPTO_BACKEND "WinCNG" CACHE STRING "" FORCE)
else ()
    set(CRYPTO_BACKEND "OpenSSL" CACHE STRING "" FORCE)
endif ()
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_ZLIB_COMPRESSION OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
        libssh2
        GIT_REPOSITORY https://github.com/libssh2/libssh2.git
        GIT_TAG        libssh2-1.11.1
)
FetchContent_MakeAvailable(libssh2)

enable_testing()

add_subdirectory(Base)
add_subdirectory(Network)
add_subdirectory(Cryptography)
add_subdirectory(Ipc)
#add_subdirectory(Protocol)
#add_subdirectory(ProtocolRunner)