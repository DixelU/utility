cmake_minimum_required(VERSION 3.15)

project(utils)
include_directories(include)

set(CMAKE_CXX_STANDARD 23)

add_executable(test_x tests/test_x.cpp)
add_executable(mctx_test tests/mctx_test.cpp)