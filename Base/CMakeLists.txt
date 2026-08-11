cmake_minimum_required(VERSION 3.28)
project(NebulaBase
        VERSION 1.0.0.0
        LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HEADERS
    # 공통 기반
    Platform.h
    WindowsApi.h
    WinsockApi.h
    Macro.h
    Type.h
    Error.h
    Result.h
    Exception.h
    Handle.h

    # 코루틴 — Task/실행자/조합자
    Coroutine/Task.h
    Coroutine/IExecutor.h
    Coroutine/Event.h
    Coroutine/Detached.h
    Coroutine/Race.h
    Coroutine/WhenAny.h

    # 사용자가 상속해 쓰는 인터페이스
    Interface/ICommand.h
    Interface/IObserver.h
    Interface/ISingleton.h
)

add_library(${PROJECT_NAME} INTERFACE)
add_library(Nebula::Base ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} INTERFACE
                           $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# 헤더 온리 타깃이라 set(CMAKE_CXX_STANDARD) 는 아무 효과가 없다 — concepts/coroutines/std::format 을
# 쓰므로 요구 표준을 소비자에게 직접 전파해야 한다(과거엔 소비자가 각자 지정해 우연히 성립했다).
target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_23)

# 컴파일 대상이 없어 IDE 프로젝트 트리에서 헤더가 사라지는 것을 막는다(배포는 루트 install(DIRECTORY) 담당).
target_sources(${PROJECT_NAME} INTERFACE FILE_SET nebula_headers TYPE HEADERS
                                         BASE_DIRS ${CMAKE_SOURCE_DIR}
                                         FILES ${HEADERS})

set_target_properties(${PROJECT_NAME} PROPERTIES EXPORT_NAME Base)

# export 되는 타깃의 interface file set 은 반드시 함께 install 해야 한다(CMake 제약). BASE_DIRS 가
# 리포 루트이므로 "Base/..." 상대 경로가 유지되어, 루트의 install(DIRECTORY) 와 같은 위치에 놓인다.
install(TARGETS ${PROJECT_NAME} EXPORT NebulaTargets
        FILE_SET nebula_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})


if (TARGET GTest::gtest_main AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Test)
	add_subdirectory(Test)
endif ()
