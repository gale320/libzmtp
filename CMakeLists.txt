cmake_minimum_required(VERSION 3.12)

project(libzmtp)


set(CMAKE_C_FLAG  " -fPIC ")

set(CMAKE_INCLUDE_CURRENT_DIR ON)

file(GLOB LIBSRC src/*.c)

include_directories(src include)

add_library(zmtp SHARED ${LIBSRC})

add_executable(zmtptest test/zmtp_selftest.c )

target_link_libraries(zmtptest zmtp pthread)