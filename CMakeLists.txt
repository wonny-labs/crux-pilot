cmake_minimum_required(VERSION 3.28)

project(crux_pilot LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(CRUX_BUILD_HOST_SAMPLE "Build the host Time command-line sample" ON)

add_subdirectory(components/base)
add_subdirectory(components/time)

if(CRUX_BUILD_HOST_SAMPLE OR BUILD_TESTING)
    add_library(crux_time_commands STATIC samples/time/src/time_command.c)
    target_include_directories(crux_time_commands
        PUBLIC samples/time/include
        PRIVATE components/time/include
    )
    target_link_libraries(crux_time_commands PUBLIC crux::base)
    target_compile_options(crux_time_commands PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(CRUX_BUILD_HOST_SAMPLE)
    add_subdirectory(samples/time/host)
endif()

if(BUILD_TESTING)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/b514bdc898e2951020cbdca1304b75f5950d1f59.tar.gz
        URL_HASH SHA256=9257316d65d6259f6596bc4fa5464092305ab3f3d9dc6d7e5780ab462d8baa64
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "Use the shared CRT on Windows" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests/time)
endif()
