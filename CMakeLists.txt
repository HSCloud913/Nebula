cmake_minimum_required(VERSION 3.28)
project(Nebula
        VERSION 1.0.0.0)

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_subdirectory(Base)
# add_subdirectory(Cryptography)
# add_subdirectory(Protocol)