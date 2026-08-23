cmake_minimum_required(VERSION 3.28)
project(NebulaCompressTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME} test_compress.cpp test_decompress.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE NebulaCompress GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})
