cmake_minimum_required(VERSION 3.28)
project(NebulaBase
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HEADERS
        Exception.h
        Handle.h

        Interface/ICommand.h
        Interface/IObserver.h
        Interface/ISingleton.h
)

set(SOURCES
        Error.cpp
)

add_library(${PROJECT_NAME} STATIC ${HEADERS} ${SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/Base)

add_subdirectory(Test)
