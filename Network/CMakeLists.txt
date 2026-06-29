cmake_minimum_required(VERSION 3.28)
project(NebulaNetwork VERSION 1.0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BASE_DIR "${CMAKE_SOURCE_DIR}/Base")

set(SOURCES
    Socket/Socket.cpp
    Stream/PlainStream.cpp
    Http/Http1Client.cpp
    Http/Http1Server.cpp
)

if (UNIX)
    list(APPEND SOURCES IoEngine/EpollEngine.cpp)
endif()

if (WIN32)
    list(APPEND SOURCES IoEngine/IocpEngine.cpp)
endif()

add_library(NebulaNetwork STATIC ${SOURCES})

target_include_directories(NebulaNetwork PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}   # Network/ 루트 (NetworkType.h, IoEngine/, Socket/)
    ${BASE_DIR}                   # NebulaHandle.h, Result.h, Error.h, Type.h
)

target_link_libraries(NebulaNetwork PUBLIC NebulaBase)

if (WIN32)
    target_link_libraries(NebulaNetwork PRIVATE Ws2_32)
endif()
