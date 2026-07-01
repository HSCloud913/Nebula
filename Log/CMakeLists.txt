cmake_minimum_required(VERSION 3.28)
project(NebulaLog
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME} STATIC Logger.h Logger.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Base
        ${CMAKE_SOURCE_DIR}/Concurrency
)
target_link_libraries(${PROJECT_NAME} PUBLIC NebulaBase NebulaConcurrency)


if (TARGET GTest::gtest_main AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Test)
    add_subdirectory(Test)
endif ()