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

add_library(${PROJECT_NAME} STATIC ${HEADERS} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Base)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Ipc)

if (WIN32)
    target_sources(${PROJECT_NAME} PRIVATE ${WINDOWS_HEADERS} ${WINDOWS_SOURCES})
    target_link_libraries(${PROJECT_NAME} PRIVATE Ws2_32 crypt32 bcrypt NebulaBase)
else ()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread rt NebulaBase)
endif ()

add_subdirectory(Test)