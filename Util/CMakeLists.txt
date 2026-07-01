cmake_minimum_required(VERSION 3.28)
project(NebulaUtil
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HEADERS
        Ascii.h
        Base64.h
        StringFormat.h
)

set(SOURCES
        Ascii.cpp
        Base64.cpp
)

add_library(NebulaUtil STATIC ${HEADERS} ${SOURCES})
target_include_directories(NebulaUtil PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Base)
target_link_libraries(NebulaUtil PUBLIC NebulaBase)


if(TARGET GTest::gtest_main AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Test)
    add_subdirectory(Test)
endif()
