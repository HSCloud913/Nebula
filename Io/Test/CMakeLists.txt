cmake_minimum_required(VERSION 3.28)
project(NebulaIoTest LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
        test.cpp
        test_mmap.cpp
        test_buffer_chain.cpp
        test_engine.cpp
        test_iocontext.cpp
        test_ioresult.cpp
        test_ioawaitable.cpp
        test_file.cpp
        test_iosocket.cpp
)
# test_asyncfile.cpp / test_engine_integration.cpp 은 구 엔진 인터페이스 의존 —
# Level 1~3 재구축(Phase 3~5) 때 새 모델로 되살린다.
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
