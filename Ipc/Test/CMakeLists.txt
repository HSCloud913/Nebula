cmake_minimum_required(VERSION 3.28)
project(NebulaIpcTest LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
               test.cpp
               test_pipe.cpp
               test_sharedmemory.cpp
               test_messagequeue.cpp
               test_semaphore.cpp
)
target_link_libraries(${PROJECT_NAME} PUBLIC NebulaBase NebulaUtil NebulaIo NebulaIpc GTest::gtest_main)

if (WIN32)
	target_link_libraries(${PROJECT_NAME} PRIVATE Ws2_32 crypt32 bcrypt)
else ()
	find_package(OpenSSL REQUIRED)
	target_link_libraries(${PROJECT_NAME} PRIVATE pthread rt)
endif ()


include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})
