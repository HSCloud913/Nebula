cmake_minimum_required(VERSION 3.28)
project(NebulaIo
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HEADERS
        IoType.h
        IoError.h
        IoResult.h
        Context/Context.h
        Coroutine/Awaitable.h
        Coroutine/Timeout.h
        File/File.h
        Socket/Socket.h

        Buffer/BufferView.h
        Buffer/BufferChain.h
        Buffer/RegisteredBuffer.h
        Buffer/BufferPool.h

        Context/Operation.h
        Engine/IEngine.h
        Engine/IRegisteredBufferProvider.h
        Engine/IoUring/IoUringEngine.h
        Engine/IoUring/Provider/IoUringProvider.h
        Engine/Epoll/EpollEngine.h
        Engine/Iocp/IocpEngine.h
        Engine/Iocp/Provider/RioExtension.h
        Engine/Iocp/Provider/RioProvider.h
        Engine/WsaPoll/WsaPollEngine.h
)

set(SOURCES
        Context/Context.cpp
        File/File.cpp
        Socket/Socket.cpp
        Buffer/BufferPool.cpp
)

if (WIN32)
    list(APPEND SOURCES Engine/Iocp/IocpEngine.cpp)
    list(APPEND SOURCES Engine/WsaPoll/WsaPollEngine.cpp)
    list(APPEND SOURCES Engine/Iocp/Provider/RioProvider.cpp)
elseif (UNIX AND NOT APPLE)
    list(APPEND SOURCES Engine/Epoll/EpollEngine.cpp)
    list(APPEND SOURCES Engine/IoUring/IoUringEngine.cpp)
    list(APPEND SOURCES Engine/IoUring/Provider/IoUringProvider.cpp)
endif ()

add_library(${PROJECT_NAME} STATIC ${HEADERS} ${SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Base
        ${CMAKE_SOURCE_DIR}/Concurrency
        ${CMAKE_SOURCE_DIR}/Time
        ${CMAKE_SOURCE_DIR}/Memory
)
target_link_libraries(${PROJECT_NAME} PUBLIC NebulaBase NebulaConcurrency NebulaTime NebulaMemory)

if (MSVC)
    # 이 저장소 소스는 BOM 없는 UTF-8 이라, /utf-8 없이는 MSVC 가 시스템 코드페이지(한국어 Windows
    # 는 CP949)로 소스를 잘못 해석한다. 한글 주석의 멀티바이트 시퀀스가 깨지면 토큰화 자체가
    # 어긋나서, 전혀 상관없어 보이는 곳(예: 함수 끝의 return 문)에서 원인을 알기 어려운 연쇄
    # 구문 오류(C4716 등)가 난다. PUBLIC 으로 전파해 NebulaIo 를 쓰는 소비자도 같은 문제를 피한다.
    target_compile_options(${PROJECT_NAME} PUBLIC /utf-8)
endif ()

# IocpEngine.cpp 가 WSA*(Ws2_32) 와 TransmitFile(Mswsock) 을 호출한다. PUBLIC 으로 링크해
# NebulaIo 를 쓰는 모든 소비자(Network, Ipc, 각 Test)가 자동 상속하게 한다 —
# 소비자마다 Mswsock 을 빠뜨려 링크 실패하는 문제를 원천 차단.
if (WIN32)
    # ntdll: RtlNtStatusToDosError (완료 entry.Internal NTSTATUS → Win32 에러코드 변환)
    target_link_libraries(${PROJECT_NAME} PUBLIC Ws2_32 Mswsock ntdll)
endif ()

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
