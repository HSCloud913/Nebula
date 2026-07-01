cmake_minimum_required(VERSION 3.28)
project(NebulaIoTest LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


add_executable(NebulaIoTest
        test.cpp
        test_asyncfile.cpp
        test_mmap.cpp
        test_engine_integration.cpp
)
target_link_libraries(NebulaIoTest PRIVATE NebulaBase NebulaIo GTest::gtest_main)

if(WIN32)
    target_link_libraries(NebulaIoTest PRIVATE Ws2_32)
endif()


include(GoogleTest)
gtest_discover_tests(NebulaIoTest)
