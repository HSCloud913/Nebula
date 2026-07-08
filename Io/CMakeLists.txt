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
        IoContext.h
        Coroutine/IoAwaitable.h
        File/File.h
        Socket/Socket.h

        File/IIoFile.h

        Buffer/BufferBlock.h
        Buffer/BufferView.h
        Buffer/BufferChain.h

        Coroutine/Awaitable.h

        Engine/ReactorType.h
        Engine/ProactorType.h
        Engine/IoCapability.h
        Engine/IoOperation.h
        Engine/IIoEngine.h
        Engine/IRegisteredBufferProvider.h
        Engine/IoUring/IoUringEngine.h
        Engine/Epoll/EpollEngine.h
        Engine/Iocp/IocpEngine.h
        Engine/Iocp/Rio/RioExtension.h
        Engine/Iocp/Rio/RioProvider.h
)

# Phase 2: 엔진 인터페이스 완료기반 in-place 개조 중.
# 구 인터페이스(Watch/RunOnce/SubmitSend)에 의존하는 AsyncFile / RioProvider 는
# Level 1~3.5 재구축(Phase 3~7) 때 새 모델로 되살린다 — 지금은 빌드에서 제외.
set(SOURCES
        IoContext.cpp
        File/File.cpp
        File/MemoryMapFile.cpp
        Socket/Socket.cpp
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
        ${CMAKE_SOURCE_DIR}/Memory
)
target_link_libraries(${PROJECT_NAME} PUBLIC NebulaBase NebulaConcurrency NebulaTime NebulaMemory)

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
