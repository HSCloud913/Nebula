cmake_minimum_required(VERSION 3.28)
project(NebulaNetworkTest
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
               test.cpp
               test_socket.cpp
               test_tls_config.cpp
               test_tls_stream.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
                           ${CMAKE_SOURCE_DIR}/Network
                           ${CMAKE_SOURCE_DIR}/Base
)

target_link_libraries(${PROJECT_NAME} gtest gtest_main NebulaNetwork)

if (WIN32)
	target_link_libraries(${PROJECT_NAME} Ws2_32 Secur32 Crypt32)
endif ()

if (UNIX)
	find_package(OpenSSL QUIET)
	if (TARGET OpenSSL::SSL)
		target_compile_definitions(${PROJECT_NAME} PRIVATE NEBULA_WITH_OPENSSL)
		target_link_libraries(${PROJECT_NAME} OpenSSL::SSL OpenSSL::Crypto)
	endif ()
endif ()

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
