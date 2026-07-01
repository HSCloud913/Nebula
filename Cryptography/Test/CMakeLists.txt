cmake_minimum_required(VERSION 3.28)
project(NebulaCryptographyTest LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
        test.cpp
        test_aes.cpp
        test_hash.cpp
        test_rsa.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE NebulaBase NebulaUtil NebulaCryptography GTest::gtest_main)


include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})