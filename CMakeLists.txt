cmake_minimum_required(VERSION 3.15)

project(utils LANGUAGES CXX)
include_directories(include)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
find_package(Threads REQUIRED)

# if (UNIX)
#	add_compile_options(-fsanitize=address -fsanitize-address-use-after-scope -fsanitize=leak -fsanitize=undefined -fsanitize=pointer-overflow -fsanitize=signed-integer-overflow -fsanitize=alignment -fstack-protector-strong)
#	add_link_options(-fsanitize=address -fsanitize-address-use-after-scope -fsanitize=leak -fsanitize=undefined -fsanitize=pointer-overflow -fsanitize=signed-integer-overflow -fsanitize=alignment -fstack-protector-strong)
# endif ()

set (src
	src/mctx.cpp
	src/mctx_json.cpp
)

add_executable(test_x
	tests/test_x.cpp
	${src}
)

add_executable(mctx_test
	tests/mctx_test.cpp
	${src}
)

add_executable(mctx_raw_stress_test
	tests/mctx_raw_stress_test.cpp
	${src}
)

add_executable(collected_utilities_test
	tests/collected_utilities_test.cpp
	${src}
)
target_link_libraries(collected_utilities_test PRIVATE Threads::Threads)

if (BUILD_TESTING)
	add_test(NAME mctx_test COMMAND mctx_test)
	add_test(NAME mctx_raw_stress_test COMMAND mctx_raw_stress_test)
	add_test(NAME collected_utilities_test COMMAND collected_utilities_test)
endif ()
