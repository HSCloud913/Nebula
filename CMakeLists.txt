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

# include 정리(모듈 접두 경로): 모든 내부 헤더는 리포 루트 기준 "Module/..." 로 포함한다.
# 예: #include "Io/Coroutine/Awaitable.h", "Base/Type.h". 이렇게 하면 Io/Coroutine/Awaitable.h 와
# Time/Coroutine/Awaitable.h 같은 동명(同名) 헤더가 모듈 접두로 유일하게 구분된다.
include_directories(${CMAKE_SOURCE_DIR})

# 의존 순서대로 추가
add_subdirectory(Base)
add_subdirectory(Memory)
add_subdirectory(Concurrency)
add_subdirectory(Log)
add_subdirectory(Json)
add_subdirectory(Util)
add_subdirectory(Time)
add_subdirectory(Io)
add_subdirectory(Cryptography)
add_subdirectory(Ipc)
add_subdirectory(Network)
# Compress 는 구현 후 추가
