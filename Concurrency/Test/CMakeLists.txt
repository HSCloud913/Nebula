cmake_minimum_required(VERSION 3.28)
project(NebulaConcurrencyTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(NebulaConcurrencyTest
    test.cpp
    test_mpsc.cpp
    test_spsc.cpp
)
target_link_libraries(NebulaConcurrencyTest PRIVATE
    NebulaConcurrency
    GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(NebulaConcurrencyTest)
