cmake_minimum_required(VERSION 4.3)
project(bitforce CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point OPENSSL_ROOT_DIR at the install if CMake can't find it, e.g. C:/vcpkg/installed/x64-windows
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)

add_executable(bitforce bitforce.cpp)
target_link_libraries(bitforce PRIVATE OpenSSL::Crypto)
