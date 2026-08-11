cmake_minimum_required(VERSION 3.28)
project(NebulaIpc
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(SOURCES
    Pipe.cpp
    SharedMemory.cpp
    MessageQueue.cpp
    Semaphore.cpp
)

add_library(${PROJECT_NAME} STATIC ${SOURCES})
add_library(Nebula::Ipc ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PUBLIC
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           ${CMAKE_SOURCE_DIR}/Base
                           ${CMAKE_SOURCE_DIR}/Util)
# Io 는 Pipe/MessageQueue 의 .cpp 구현 세부에서만 쓰인다(공개 헤더 노출 0) — PRIVATE 로 좁힌다.
target_link_libraries(${PROJECT_NAME} PUBLIC NebulaBase NebulaUtil)
target_link_libraries(${PROJECT_NAME} PRIVATE NebulaIo)

if (WIN32)
	target_link_libraries(${PROJECT_NAME} PRIVATE Ws2_32 crypt32 bcrypt)
else ()
	find_package(OpenSSL REQUIRED)
	target_link_libraries(${PROJECT_NAME} PRIVATE pthread rt)
endif ()


if (TARGET GTest::gtest_main AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Test)
	add_subdirectory(Test)
endif ()
