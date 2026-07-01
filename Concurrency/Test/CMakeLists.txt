cmake_minimum_required(VERSION 3.28)
project(NebulaConcurrencyTest LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
        test.cpp
        test_mpsc.cpp
        test_spsc.cpp
        test_threadpool.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE NebulaBase NebulaConcurrency GTest::gtest_main)


include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})
