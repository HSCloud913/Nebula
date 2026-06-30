cmake_minimum_required(VERSION 3.28)
project(NebulaUtil VERSION 1.0.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(NebulaUtil STATIC
    Ascii.cpp
    Base64.cpp
)
target_include_directories(NebulaUtil PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Base
)
target_link_libraries(NebulaUtil PUBLIC NebulaBase)

if(TARGET GTest::gtest_main AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Test)
    add_subdirectory(Test)
endif()
