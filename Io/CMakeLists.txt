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

        Engine/IIoEngine.h
        Engine/Awaitable.h
        Engine/IoUring/IoUringEngine.h
        Engine/Epoll/EpollEngine.h
        Engine/Iocp/IocpEngine.h
)

set(SOURCES
        File/AsyncFile.cpp
        File/MemoryMapFile.cpp
)

if(UNIX AND NOT APPLE)
    list(APPEND SOURCES Engine/Epoll/EpollEngine.cpp)
    list(APPEND SOURCES Engine/IoUring/IoUringEngine.cpp)
elseif(WIN32)
    list(APPEND SOURCES Engine/Iocp/IocpEngine.cpp)
endif()

add_library(NebulaIo STATIC ${HEADERS} ${SOURCES})
target_include_directories(NebulaIo PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Base
)
target_link_libraries(NebulaIo PUBLIC NebulaBase NebulaTime NebulaConcurrency)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBURING REQUIRED liburing)
    target_link_libraries(NebulaIo PUBLIC ${LIBURING_LIBRARIES})
    target_include_directories(NebulaIo PUBLIC ${LIBURING_INCLUDE_DIRS})
    target_link_libraries(NebulaIo PRIVATE pthread)
elseif(UNIX)
    target_link_libraries(NebulaIo PRIVATE pthread)
endif()

if(TARGET GTest::gtest_main AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Test)
    add_subdirectory(Test)
endif()
