cmake_minimum_required(VERSION 3.28)
project(NebulaUtilTest LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(NebulaUtilTest
        test.cpp
        test_ascii.cpp
        test_base64.cpp
        test_stringformat.cpp
)
target_link_libraries(NebulaUtilTest PRIVATE NebulaBase NebulaUtil GTest::gtest_main)


include(GoogleTest)
gtest_discover_tests(NebulaUtilTest)
