cmake_minimum_required(VERSION 3.28)
project(NebulaCompress
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HEADERS
    # 공개 진입점
    Codec.h
    Deflate.h
    Gzip.h

    # 공개 값 타입
    Diagnostic/Error.h

    # 내부 구현
    Internal/BitReader.h
    Internal/BitWriter.h
    Internal/Checksum.h
    Internal/Encode.h
    Internal/Huffman.h
    Internal/Inflate.h
    Internal/Tables.h
)

set(SOURCES
    Codec.cpp
    Deflate.cpp
    Gzip.cpp
    Internal/Checksum.cpp
    Internal/Encode.cpp
    Internal/Huffman.cpp
    Internal/Inflate.cpp
)

add_library(${PROJECT_NAME} STATIC ${HEADERS} ${SOURCES})
add_library(Nebula::Compress ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

set_target_properties(${PROJECT_NAME} PROPERTIES EXPORT_NAME Compress)
install(TARGETS ${PROJECT_NAME} EXPORT NebulaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Util 은 Codec.cpp 의 Ascii(토큰 정규화)뿐이라 PRIVATE — 공개 헤더는 Base 만 들인다.
target_link_libraries(${PROJECT_NAME} PUBLIC NebulaBase)
target_link_libraries(${PROJECT_NAME} PRIVATE NebulaUtil)


if (TARGET GTest::gtest_main AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Test)
	add_subdirectory(Test)
endif ()
