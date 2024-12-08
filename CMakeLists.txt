cmake_minimum_required(VERSION 3.21)
project(cpp-io-uring-proactor VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE SRCS
    src/**.cpp
)

add_executable(cpp-io-uring-proactor ${SRCS})

target_compile_options(cpp-io-uring-proactor PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wattributes
    -Wconversion
    -Wduplicated-cond
    -Wduplicated-branches
    -Wformat
    -Wimplicit-fallthrough
    -Wpedantic
)

find_library(LIB_RT NAMES rt REQUIRED)
find_library(LIB_IO_URING NAMES uring)

target_include_directories(cpp-io-uring-proactor PRIVATE
    src/
)

target_link_libraries(cpp-io-uring-proactor PRIVATE
    ${LIB_RT}
    ${LIB_IO_URING}
)
