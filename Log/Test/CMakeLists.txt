cmake_minimum_required(VERSION 3.28)
project(NebulaLogTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(NebulaLogTest
    test.cpp
    test_logger.cpp
)
target_include_directories(NebulaLogTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Log
    ${CMAKE_SOURCE_DIR}/Base
    ${CMAKE_SOURCE_DIR}/Concurrency
)
target_link_libraries(NebulaLogTest PRIVATE
    NebulaLog
    GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(NebulaLogTest)
