cmake_minimum_required(VERSION 3.28)
project(NebulaTimeTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(NebulaTimeTest
    test.cpp
    test_clock.cpp
    test_timerwheel.cpp
)
target_link_libraries(NebulaTimeTest PRIVATE
    NebulaTime
    GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(NebulaTimeTest)
