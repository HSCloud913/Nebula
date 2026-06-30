cmake_minimum_required(VERSION 3.28)
project(NebulaConcurrency
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HEADERS
    Queue/MpscQueue.h
    Queue/SpscQueue.h
)

add_library(NebulaConcurrency INTERFACE)
target_include_directories(NebulaConcurrency INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Base
)

if(TARGET GTest::gtest_main)
    add_subdirectory(Test)
endif()
