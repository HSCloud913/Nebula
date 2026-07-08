cmake_minimum_required(VERSION 3.28)
project(NebulaIoTest LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
        test.cpp
        test_buffer_chain.cpp
        test_engine.cpp
        test_iocontext.cpp
        test_ioresult.cpp
        test_ioawaitable.cpp
        test_file.cpp
        test_iosocket.cpp
)

if (WIN32)
    target_sources(${PROJECT_NAME} PRIVATE test_wsapoll_engine.cpp)
endif ()
target_link_libraries(${PROJECT_NAME} PRIVATE NebulaBase NebulaConcurrency NebulaTime NebulaMemory NebulaIo GTest::gtest_main)

if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE Ws2_32 Mswsock)
elseif (UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBURING REQUIRED liburing)
    target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBURING_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PUBLIC ${LIBURING_INCLUDE_DIRS})
endif ()


include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})
