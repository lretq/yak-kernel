cmake_minimum_required(VERSION 3.26)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/toolchains/Generic.cmake)

project(Yak
    VERSION 0.0.0
    HOMEPAGE_URL https://github.com/lretq/yak-src
    LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED True)

set(YAK_SOURCES "")
set(YAK_INCLUDES "")

macro(yak_add_sources)
    foreach(SOURCE_FILE ${ARGV})
        target_sources(kernel PRIVATE "${CMAKE_CURRENT_LIST_DIR}/${SOURCE_FILE}")
    endforeach()
endmacro()

macro(yak_add_includes)
    foreach(INC_DIR ${ARGV})
        target_include_directories(kernel PRIVATE "${CMAKE_CURRENT_LIST_DIR}/${INC_DIR}")
    endforeach()
endmacro()

add_subdirectory(kernel)
add_subdirectory(vendor)
