cmake_minimum_required(VERSION 3.28)
project(NebulaMemoryTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(NebulaMemoryTest
               test_allocator.cpp
)
target_link_libraries(NebulaMemoryTest PRIVATE
                      NebulaMemory
                      GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(NebulaMemoryTest)
