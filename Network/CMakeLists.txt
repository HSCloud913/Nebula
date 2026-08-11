cmake_minimum_required(VERSION 3.28)
project(NebulaNetwork VERSION 1.0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
        Dns.cpp
        Stream/PlainStream.cpp
        Stream/Tls/TlsStream.cpp
        Protocol/Http/ClientBuilder.cpp
        Protocol/Http/ServerBuilder.cpp
        Protocol/Http/Message/Method.cpp
        Protocol/Http/Message/Status.cpp
        Protocol/Http/Message/Params.cpp
        Protocol/Http/Internal/Router.cpp
        Protocol/Http/Internal/Transport.cpp
        Protocol/Http/Internal/Http1/Client.cpp
        Protocol/Http/Internal/Http1/Parser.cpp
        Protocol/Http/Internal/Http1/Server.cpp
        Protocol/Http/Internal/Http2/Frame.cpp
        Protocol/Http/Internal/Http2/Hpack.cpp
        Protocol/Http/Internal/Http2/Connection.cpp
)

# TLS 백엔드는 플랫폼당 하나만 컴파일한다 — 두 구현이 같은 TlsStream 메서드를 정의하므로
# 동시에 넣으면 중복 정의가 된다(Io 의 엔진 백엔드 선택과 같은 구조).
if (WIN32)
	list(APPEND SOURCES
	     Stream/Tls/Internal/Schannel/SchannelStream.cpp
	     Stream/Tls/Internal/Schannel/TlsMessageBuffer.cpp
	)
else ()
	list(APPEND SOURCES
	     Stream/Tls/Internal/OpenSsl/OpenSslStream.cpp
	)
endif ()

add_library(${PROJECT_NAME} STATIC ${SOURCES})
add_library(Nebula::Network ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# 실제 소스 의존: Base(타입/코루틴/Event), Memory(BufferView·IAllocator — 공개 헤더 include),
# Util(Header.h 의 StringFormat — 공개 헤더 include), Io(Stream/Http 전반 — 공개 헤더 include).
# Concurrency 는 Dns.cpp 내부의 ThreadPool 뿐이라 PRIVATE. Time 은 include 근거 0(Io 가 전파)이라 제거(2026-08-03).
target_link_libraries(${PROJECT_NAME} PUBLIC
                      NebulaBase
                      NebulaMemory
                      NebulaUtil
                      NebulaIo
)
target_link_libraries(${PROJECT_NAME} PRIVATE NebulaConcurrency)

if (WIN32)
	target_link_libraries(${PROJECT_NAME} PRIVATE Ws2_32 Secur32 Crypt32 Mswsock)
endif ()

if (UNIX)
	find_package(OpenSSL QUIET)
	if (TARGET OpenSSL::SSL)
		# NEBULA_WITH_OPENSSL 은 공개 헤더(TlsStream.h)의 멤버 구성을 바꾸므로 반드시 PUBLIC —
		# PRIVATE 이면 소비자가 다른 레이아웃으로 컴파일해 ODR/ABI 가 깨진다.
		target_compile_definitions(${PROJECT_NAME} PUBLIC NEBULA_WITH_OPENSSL)
		target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
	endif ()
endif ()

if (TARGET GTest::gtest_main AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Test)
	add_subdirectory(Test)
endif ()