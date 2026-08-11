cmake_minimum_required(VERSION 3.28)
project(Nebula
        VERSION 1.0.0.0)

include(GNUInstallDirs)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
		googletest
		GIT_REPOSITORY https://github.com/google/googletest.git
		GIT_TAG v1.15.2
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# libssh2 는 SshStream 구현이 실제로 시작될 때 다시 추가한다 — 사용처 0 인 상태로 FetchContent
# 하면 클린 빌드마다 clone/build 비용만 낭비된다(2026-08-03 제거, 소스 내 NEBULA_WITH_LIBSSH2 참조 0건 확인).

# 소스 전반이 UTF-8(한글 주석 포함)이므로, MSVC 가 로캘 코드페이지로 잘못 해석해 실제 컴파일
# 에러로 번지는 것을 막기 위해 소스/실행 문자셋을 UTF-8 로 강제한다. googletest 는 이미
# 위에서 add_subdirectory 됐으므로 이 옵션의 영향을 받지 않는다(이후 추가되는 서브디렉터리부터 적용).
if (MSVC)
	add_compile_options(/utf-8)
endif ()

enable_testing()

# include 정리(모듈 접두 경로): 모든 내부 헤더는 리포 루트 기준 "Module/..." 로 포함한다.
# 예: #include "Io/Coroutine/Awaitable.h", "Base/Type.h". 이렇게 하면 Io/Coroutine/Awaitable.h 와
# Time/Sleep.h 같은 동명(同名) 헤더가 모듈 접두로 유일하게 구분된다.
include_directories(${CMAKE_SOURCE_DIR})

# 의존 순서대로 추가
add_subdirectory(Base)
add_subdirectory(Memory)
add_subdirectory(Concurrency)
add_subdirectory(Log)
add_subdirectory(Json)
add_subdirectory(Util)
add_subdirectory(Time)
add_subdirectory(Io)
add_subdirectory(Cryptography)
add_subdirectory(Ipc)
add_subdirectory(Network)
# Compress 는 구현 후 추가


# ── install / export: find_package(Nebula CONFIG) 지원 ──
# export 범위: 핵심(Base~Time) + Io + Cryptography. Network/Ipc 는 외부 의존성(OpenSSL/liburing) 정리 후 추가.
# **어떤 Internal/ 도 공개 헤더가 include 하지 않는다** — 그래서 모든 Internal/ 을 배포에서 제외한다.
# (예외였던 둘은 실체가 공개 표면이라 승격해 해소: Base/Coroutine/Internal 의 Race → Coroutine/Race.h
#  2026-08-06, Concurrency/Internal 의 SingleRoleGuard → Queue/SingleRoleGuard.h 2026-08-11.
#  둘 다 값 멤버/전이 include 라 숨길 수 없는 구조였고, Internal 이라는 위치가 거짓말이었던 경우다.)
install(DIRECTORY Base Memory Concurrency Log Json Util Time Io Cryptography
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
        REGEX "/Internal/" EXCLUDE
        REGEX "/Test" EXCLUDE)

install(EXPORT NebulaTargets
        NAMESPACE Nebula::
        FILE NebulaTargets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Nebula)

include(CMakePackageConfigHelpers)
configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/NebulaConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/NebulaConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Nebula)
write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/NebulaConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/NebulaConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/NebulaConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Nebula)
