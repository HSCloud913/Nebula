cmake_minimum_required(VERSION 3.28)
project(NebulaCryptography
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header only 항목만 추가
set(HEADERS

)

set(SOURCES
        Base64/Base64.cpp

        Hash/CRC32.cpp
        Hash/MD5.cpp
        Hash/SHA1.cpp
        Hash/SHA2.cpp
        Hash/SHA3.cpp
        Hash/Wrapper/CRC32Wrapper.cpp
        Hash/Wrapper/HashWrapper.cpp
        Hash/Wrapper/MD5Wrapper.cpp
        Hash/Wrapper/SHA1Wrapper.cpp
        Hash/Wrapper/SHA2Wrapper.cpp
        Hash/Wrapper/SHA3Wrapper.cpp
        Hash/Factory/HashFactory.cpp
)

add_library(${PROJECT_NAME} STATIC ${HEADERS} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Base)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/Cryptography)
target_link_libraries(${PROJECT_NAME} PRIVATE NebulaBase)

add_subdirectory(Test)