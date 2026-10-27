cmake_minimum_required(VERSION 4.3)
project(bitforce CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point OPENSSL_ROOT_DIR at the install if CMake can't find it, e.g. C:/vcpkg/installed/x64-windows
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)

# long_uint.h from the enclosing repository.
include_directories(../../include)

add_executable(bitforce bitforce.cpp)
target_link_libraries(bitforce PRIVATE OpenSSL::Crypto)

include(CTest)
if(BUILD_TESTING)
    add_executable(bitforce_tests bitforce_tests.cpp)
    target_link_libraries(bitforce_tests PRIVATE OpenSSL::Crypto)
    add_test(NAME bitforce_addresses COMMAND bitforce_tests)
    add_test(NAME bitforce_derive COMMAND bitforce --derive
        0000000000000000000000000000000000000000000000000000000000000001)
    set_tests_properties(bitforce_derive PROPERTIES
        PASS_REGULAR_EXPRESSION "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4")
    add_test(NAME bitforce_reject_zero COMMAND bitforce --derive
        0000000000000000000000000000000000000000000000000000000000000000)
    add_test(NAME bitforce_reject_order COMMAND bitforce --derive
        fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141)
    add_test(NAME bitforce_reject_hex COMMAND bitforce --derive
        000000000000000000000000000000000000000000000000000000000000000g)
    add_test(NAME bitforce_reject_length COMMAND bitforce --derive 1)
    set_tests_properties(bitforce_reject_zero bitforce_reject_order
        bitforce_reject_hex bitforce_reject_length PROPERTIES WILL_FAIL TRUE)
endif()
