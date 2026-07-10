cmake_minimum_required(VERSION 3.28)
project(NebulaBaseTest LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
               test.cpp
               test_command.cpp
               test_exception.cpp
               test_handle.cpp
               test_observer.cpp
               test_singleton.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE NebulaBase GTest::gtest_main)


include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})
