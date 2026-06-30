cmake_minimum_required(VERSION 3.28)
project(NebulaIpcTest
        VERSION 1.0.0.0
        LANGUAGES CXX)

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

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Base)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Ipc)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Network)
target_link_libraries(${PROJECT_NAME} gtest gtest_main NebulaBase NebulaIpc NebulaNetwork)

if (WIN32)
    target_link_libraries(${PROJECT_NAME} Ws2_32 crypt32 bcrypt)
else ()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(${PROJECT_NAME} pthread rt)
endif ()

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})