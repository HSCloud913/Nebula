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
    Hex.h
    SecureRandom.h
    SecureWipe.h
    StringFormat.h
)

set(SOURCES
    Ascii.cpp
    Base64.cpp
    SecureRandom.cpp
)

add_library(${PROJECT_NAME} STATIC ${HEADERS} ${SOURCES})
add_library(Nebula::Util ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

set_target_properties(${PROJECT_NAME} PROPERTIES EXPORT_NAME Util)
install(TARGETS ${PROJECT_NAME} EXPORT NebulaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
target_link_libraries(${PROJECT_NAME} PUBLIC NebulaBase)

# SecureRandom: Windows 는 BCryptGenRandom(bcrypt), POSIX 는 getrandom(2)(libc 내장).
if (WIN32)
	target_link_libraries(${PROJECT_NAME} PRIVATE bcrypt)
endif ()


if (TARGET GTest::gtest_main AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Test)
	add_subdirectory(Test)
endif ()
