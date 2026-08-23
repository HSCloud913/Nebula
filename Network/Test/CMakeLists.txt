cmake_minimum_required(VERSION 3.28)
project(NebulaNetworkTest
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
               env_winsock.cpp # WSAStartup/WSACleanup 전역 환경 — 소켓 테스트의 선행 조건
               test_socket.cpp
               test_tls_config.cpp
               test_tls_stream.cpp
               test_http_date.cpp
               test_http_content_encoding.cpp
               test_http_observer.cpp
               test_http_type.cpp
               test_http_router.cpp
               test_http1_client_server.cpp
               test_http2_client_server.cpp
)

# include 경로는 NebulaNetwork 의 INTERFACE(저장소 루트)로 충분하다 — 모듈 접두 include 규약.
target_link_libraries(${PROJECT_NAME} gtest gtest_main NebulaNetwork NebulaCompress)

if (WIN32)
	target_link_libraries(${PROJECT_NAME} Ws2_32 Secur32 Crypt32)
endif ()

# NEBULA_WITH_OPENSSL 은 NebulaNetwork 가 PUBLIC 으로 전파하므로 여기서 재정의하지 않는다.
if (UNIX)
	find_package(OpenSSL QUIET)
	if (TARGET OpenSSL::SSL)
		target_link_libraries(${PROJECT_NAME} OpenSSL::SSL OpenSSL::Crypto)
	endif ()
endif ()

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
