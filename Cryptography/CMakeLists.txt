cmake_minimum_required(VERSION 3.28)
project(NebulaCryptography
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HEADERS
    # 공개 진입점(루트) — 암호 연산 표면
		Hash.h
    HMAC.h
    Kdf.h
    AES.h
    RSA.h
    Diagnostic/Error.h

    # 내부 구현
    Internal/ConstantTime.h
    Internal/Ecb.h
    Internal/HashAdapter.h
    Internal/HashFactory.h
        Internal/Math/BigInt.h
    Internal/Algorithm/CRC32.h
    Internal/Algorithm/MD5.h
    Internal/Algorithm/SHA1.h
    Internal/Algorithm/SHA2.h
    Internal/Algorithm/SHA3.h
)

set(SOURCES
		Hash.cpp
    HMAC.cpp
    Kdf.cpp
    AES.cpp
    RSA.cpp
    Internal/HashFactory.cpp
        Internal/Math/BigInt.cpp
    Internal/Algorithm/CRC32.cpp
    Internal/Algorithm/MD5.cpp
    Internal/Algorithm/SHA1.cpp
    Internal/Algorithm/SHA2.cpp
    Internal/Algorithm/SHA3.cpp
)

add_library(${PROJECT_NAME} STATIC ${HEADERS} ${SOURCES})
add_library(Nebula::Cryptography ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

set_target_properties(${PROJECT_NAME} PROPERTIES EXPORT_NAME Cryptography)
install(TARGETS ${PROJECT_NAME} EXPORT NebulaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# 실제 소스 의존: Base(타입), Util(SecureRandom — RSA/BigInt 의 .cpp 내부에서만 사용, 공개 헤더 노출 0 → PRIVATE).
target_link_libraries(${PROJECT_NAME} PUBLIC NebulaBase)
target_link_libraries(${PROJECT_NAME} PRIVATE NebulaUtil)


if (TARGET GTest::gtest_main AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Test)
	add_subdirectory(Test)
endif ()