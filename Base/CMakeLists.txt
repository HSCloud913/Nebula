cmake_minimum_required(VERSION 3.28)
project(NebulaBase
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

SET(SOURCES
        Ascii.cpp
)

add_library(${PROJECT_NAME} STATIC ${SOURCES})

add_subdirectory(Test)
