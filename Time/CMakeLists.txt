cmake_minimum_required(VERSION 3.28)
project(NebulaTime
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HEADERS
    HttpDate.h
    Sleep.h
    TimerQueue.h
)

set(SOURCES
    HttpDate.cpp
    TimerQueue.cpp
)

add_library(${PROJECT_NAME} STATIC ${HEADERS} ${SOURCES})
add_library(Nebula::Time ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

set_target_properties(${PROJECT_NAME} PROPERTIES EXPORT_NAME Time)
install(TARGETS ${PROJECT_NAME} EXPORT NebulaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
# std::thread/std::mutex 는 POSIX 에서 pthread 링크가 필요하다(Windows 는 no-op).
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC NebulaBase Threads::Threads)


if (TARGET GTest::gtest_main AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Test)
	add_subdirectory(Test)
endif ()
