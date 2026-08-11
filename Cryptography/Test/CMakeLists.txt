cmake_minimum_required(VERSION 3.28)
project(NebulaCryptographyTest LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
        test_aes.cpp
               test_hash.cpp
               test_kdf.cpp
               test_rsa.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE NebulaBase NebulaUtil NebulaCryptography GTest::gtest_main)

# 파일 해시 픽스처(data/hash_test.txt) 경로 주입 — 빌드 머신 어디서든 소스 트리 기준으로 찾는다.
target_compile_definitions(${PROJECT_NAME} PRIVATE NEBULA_CRYPTO_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")


include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})