cmake_minimum_required(VERSION 3.28)
project(NebulaIo
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HEADERS
        IoType.h

        File/IIoFile.h

        Coroutine/Awaitable.h

        Engine/IIoEngine.h
        Engine/IoUring/IoUringEngine.h
        Engine/Epoll/EpollEngine.h
        Engine/Iocp/IocpEngine.h
)

set(SOURCES
        File/AsyncFile.cpp
        File/MemoryMapFile.cpp
)

if (WIN32)
    list(APPEND SOURCES Engine/Iocp/IocpEngine.cpp)
elseif (UNIX AND NOT APPLE)
    list(APPEND SOURCES Engine/Epoll/EpollEngine.cpp)
    list(APPEND SOURCES Engine/IoUring/IoUringEngine.cpp)
endif ()

add_library(${PROJECT_NAME} STATIC ${HEADERS} ${SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Base
        ${CMAKE_SOURCE_DIR}/Concurrency
        ${CMAKE_SOURCE_DIR}/Time
)
target_link_libraries(${PROJECT_NAME} PUBLIC NebulaBase NebulaConcurrency NebulaTime)

if (UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBURING REQUIRED liburing)
    target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBURING_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PUBLIC ${LIBURING_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif ()


if (TARGET GTest::gtest_main AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Test)
    add_subdirectory(Test)
endif ()
