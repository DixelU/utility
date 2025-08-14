cmake_minimum_required(VERSION 3.15)

project(utils)
include_directories(include)

set(CMAKE_CXX_STANDARD 23)

if (UNIX)
	add_compile_options(-fsanitize=address -fsanitize-address-use-after-scope -fsanitize=leak -fsanitize=undefined -fsanitize=pointer-overflow -fsanitize=signed-integer-overflow -fsanitize=alignment -fstack-protector-strong)
	add_link_options(-fsanitize=address -fsanitize-address-use-after-scope -fsanitize=leak -fsanitize=undefined -fsanitize=pointer-overflow -fsanitize=signed-integer-overflow -fsanitize=alignment -fstack-protector-strong)
endif ()

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

add_executable(mctx_custom_heads_debug
	tests/mctx_custom_heads_debug.cpp
	${src}
)