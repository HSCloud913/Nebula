cmake_minimum_required(VERSION 3.28)
project(NebulaBaseTest
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
        test.cpp
        test_ascii.cpp
        test_exception.cpp
        test_handle.cpp
        test_logger.cpp
        test_stringformat.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Base)
target_link_libraries(${PROJECT_NAME} gtest gtest_main NebulaBase)

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})