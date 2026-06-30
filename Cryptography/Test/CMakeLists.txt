cmake_minimum_required(VERSION 3.28)
project(NebulaCryptographyTest
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
        test.cpp
        test_hash.cpp
        test_aes.cpp
        test_rsa.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Base)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Cryptography)
target_link_libraries(${PROJECT_NAME} gtest gtest_main NebulaBase NebulaCryptography)

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})