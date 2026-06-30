cmake_minimum_required(VERSION 3.28)
project(NebulaIoTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(NebulaIoTest
    test.cpp
    test_asyncfile.cpp
    test_mmap.cpp
    test_sendfile.cpp
)
target_link_libraries(NebulaIoTest PRIVATE
    NebulaIo
    GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(NebulaIoTest)
