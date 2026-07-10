cmake_minimum_required(VERSION 3.28)
project(NebulaCryptography
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HEADERS
    Hash/Hash.h
    Hash/Algorithm/CRC32.h
    Hash/Algorithm/MD5.h
    Hash/Algorithm/SHA1.h
    Hash/Algorithm/SHA2.h
    Hash/Algorithm/SHA3.h
    Hash/Algorithm/Wrapper/CRC32Wrapper.h
    Hash/Algorithm/Wrapper/HashWrapper.h
    Hash/Algorithm/Wrapper/MD5Wrapper.h
    Hash/Algorithm/Wrapper/SHA1Wrapper.h
    Hash/Algorithm/Wrapper/SHA2Wrapper.h
    Hash/Algorithm/Wrapper/SHA3Wrapper.h
    Hash/Factory/HashFactory.h

    Math/BigInt.h

    Random/SecureRandom.h

    HMAC/HMAC.h
    AES/AES.h
    RSA/RSA.h
)

set(SOURCES
    Hash/Hash.cpp
    Hash/Algorithm/CRC32.cpp
    Hash/Algorithm/MD5.cpp
    Hash/Algorithm/SHA1.cpp
    Hash/Algorithm/SHA2.cpp
    Hash/Algorithm/SHA3.cpp
    Hash/Algorithm/Wrapper/CRC32Wrapper.cpp
    Hash/Algorithm/Wrapper/HashWrapper.cpp
    Hash/Algorithm/Wrapper/MD5Wrapper.cpp
    Hash/Algorithm/Wrapper/SHA1Wrapper.cpp
    Hash/Algorithm/Wrapper/SHA2Wrapper.cpp
    Hash/Algorithm/Wrapper/SHA3Wrapper.cpp
    Hash/Factory/HashFactory.cpp

    Math/BigInt.cpp

    Random/SecureRandom.cpp

    HMAC/HMAC.cpp
    AES/AES.cpp
    RSA/RSA.cpp
)

add_library(${PROJECT_NAME} STATIC ${HEADERS} ${SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           ${CMAKE_SOURCE_DIR}/Base
                           ${CMAKE_SOURCE_DIR}/Util)
target_link_libraries(${PROJECT_NAME} PUBLIC NebulaBase NebulaUtil)

# SecureRandom: Windows 는 BCryptGenRandom(bcrypt) 사용. POSIX 는 getrandom(2)(libc 내장).
if (WIN32)
	target_link_libraries(${PROJECT_NAME} PUBLIC bcrypt)
endif ()


if (TARGET GTest::gtest_main AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Test)
	add_subdirectory(Test)
endif ()