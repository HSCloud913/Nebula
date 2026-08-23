cmake_minimum_required(VERSION 3.28)
project(NebulaIo
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HEADERS
    # 공개 진입점(루트) — 일상 표면
    Runtime.h
    Context.h
    ContextPool.h
    Engine.h
    Socket.h
    File.h

    # 공개 패밀리 — 코루틴 결합 계층
    Coroutine/IoOperation.h
    Coroutine/Timeout.h

    # 공개 패밀리 — 등록 버퍼(zero-copy 고급 표면)
    Buffer/RegisteredBufferProvider.h
    Buffer/RegisteredBuffer.h

    # 공개 값 타입
    Handle.h
    Diagnostic/Error.h

    # 내부 구현(엔진 백엔드)
    Internal/Engine/Iovec.h
    Internal/Engine/Iocp/IocpEngine.h
    Internal/Engine/Iocp/RioExtension.h
    Internal/Engine/Iocp/RioProvider.h
    Internal/Engine/WsaPoll/WsaPollEngine.h
    Internal/Engine/Epoll/EpollEngine.h
    Internal/Engine/IoUring/IoUringEngine.h
)

set(SOURCES
    Diagnostic/Error.cpp
    Runtime.cpp
    Context.cpp
    ContextPool.cpp
    Socket.cpp
    File.cpp
    Internal/EngineFactory.cpp
)

if (WIN32)
	list(APPEND SOURCES Internal/Engine/Iocp/IocpEngine.cpp)
	list(APPEND SOURCES Internal/Engine/Iocp/RioProvider.cpp)
	list(APPEND SOURCES Internal/Engine/WsaPoll/WsaPollEngine.cpp)
elseif (UNIX AND NOT APPLE)
	list(APPEND SOURCES Internal/Engine/Epoll/EpollEngine.cpp)
	list(APPEND SOURCES Internal/Engine/IoUring/IoUringEngine.cpp)
endif ()

add_library(${PROJECT_NAME} STATIC ${HEADERS} ${SOURCES})
add_library(Nebula::Io ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

set_target_properties(${PROJECT_NAME} PROPERTIES EXPORT_NAME Io)
install(TARGETS ${PROJECT_NAME} EXPORT NebulaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
# 실제 소스 의존: Base(타입/코루틴), Time(TimerQueue/SleepFor — Context.h 가 공개 include),
# Memory(BufferView/BufferChain — Socket/File/Operation 이 공개 include). Concurrency 는 사용
# 근거가 없어 링크하지 않는다(유령 의존 제거, 2026-08-02).
target_link_libraries(${PROJECT_NAME} PUBLIC NebulaBase NebulaTime NebulaMemory)

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
