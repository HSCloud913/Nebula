cmake_minimum_required(VERSION 3.28)
project(NebulaIpc
        VERSION 1.0.0.0
        LANGUAGES CXX)

# CMAKE_INCLUDE_CURRENT_DIR 은 켜지 않는다 — 그것이 Ipc/ 를 include 경로에 넣고, 그러면 아래
# target_include_directories 주석에 적은 Semaphore.h 충돌이 그대로 재현된다. 이 모듈은 모듈 접두
# include 만 쓰므로 현재 디렉터리가 경로에 있을 필요가 없다.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(SOURCES
    Pipe.cpp
    SharedMemory.cpp
    MessageQueue.cpp
    Semaphore.cpp
)

add_library(${PROJECT_NAME} STATIC ${SOURCES})
add_library(Nebula::Ipc ALIAS ${PROJECT_NAME})
# 루트가 이미 ${CMAKE_SOURCE_DIR} 를 넣어 주고(모듈 접두 include 규약) 이 모듈의 모든 include 가
# "Ipc/..." / "Base/..." 형태이므로, 여기서 개별 폴더를 더 얹을 이유가 없다. 오히려 유해했다 —
# Ipc/ 를 경로에 넣으면 Windows 의 대소문자 무시 파일시스템 때문에 libstdc++ 의 <semaphore> 가
# <semaphore.h> 를 들일 때 우리 Ipc/Semaphore.h 가 잡혀서, <stop_token> 을 쓰는 모든 번역 단위가
# 'sem_t does not name a type' 로 깨졌다(<stop_token> → <semaphore> → <semaphore.h>).
target_include_directories(${PROJECT_NAME} PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
# Io 는 이제 PUBLIC 이다. 비동기 API 가 ne::io::Context& 를 받고 ne::io::IoResult 를 돌려주므로
# 호출자가 Io 를 알아야 한다 — 예전 주석("공개 헤더 노출 0")은 엔진 포인터를 받던 시절의 것이고,
# 그때도 호출자는 결국 엔진을 만들어야 했으니 사실상 거짓말이었다.
target_link_libraries(${PROJECT_NAME} PUBLIC NebulaBase NebulaUtil NebulaIo)

if (WIN32)
	target_link_libraries(${PROJECT_NAME} PRIVATE Ws2_32 crypt32 bcrypt)
else ()
	find_package(OpenSSL REQUIRED)
	target_link_libraries(${PROJECT_NAME} PRIVATE pthread rt)
endif ()


if (TARGET GTest::gtest_main AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Test)
	add_subdirectory(Test)
endif ()
