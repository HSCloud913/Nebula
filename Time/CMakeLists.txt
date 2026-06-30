cmake_minimum_required(VERSION 3.28)
project(NebulaTime VERSION 1.0.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(NebulaTime STATIC
    TimerWheel.cpp
)
target_include_directories(NebulaTime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Base
)

if(PROJECT_IS_TOP_LEVEL)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Base ${CMAKE_CURRENT_BINARY_DIR}/_Base)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
    )
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

target_link_libraries(NebulaTime PUBLIC NebulaBase)

if(TARGET GTest::gtest_main)
    add_subdirectory(Test)
endif()
