cmake_minimum_required(VERSION 3.15)

project(utils LANGUAGES CXX)
include_directories(include)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
include(CheckIncludeFileCXX)
find_package(Threads REQUIRED)
check_include_file_cxx("boost/multiprecision/cpp_int.hpp" HAVE_BOOST_MULTIPRECISION)

# if (UNIX)
#	add_compile_options(-fsanitize=address -fsanitize-address-use-after-scope -fsanitize=leak -fsanitize=undefined -fsanitize=pointer-overflow -fsanitize=signed-integer-overflow -fsanitize=alignment -fstack-protector-strong)
#	add_link_options(-fsanitize=address -fsanitize-address-use-after-scope -fsanitize=leak -fsanitize=undefined -fsanitize=pointer-overflow -fsanitize=signed-integer-overflow -fsanitize=alignment -fstack-protector-strong)
# endif ()

set (src
	src/mctx.cpp
	src/mctx_json.cpp
)

set (mctx_path_src
	src/mctx_path.cpp
	src/mctx_path_traversal.cpp
	src/mctx_path_serializer.cpp
	src/linked_mctx_wrapper.cpp
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

add_executable(buffered_queue_spsc_test
	tests/buffered_queue_spsc_test.cpp
)
target_link_libraries(buffered_queue_spsc_test PRIVATE Threads::Threads)
target_compile_options(buffered_queue_spsc_test PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/EHsc>
)

add_executable(buffered_block_list_test
	tests/buffered_block_list_test.cpp
)
target_compile_options(buffered_block_list_test PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/EHsc>
)

add_executable(buffered_file_reader_test
	tests/buffered_file_reader_test.cpp
)
target_compile_options(buffered_file_reader_test PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/EHsc>
)

add_executable(matrix_utilities_test
	tests/matrix_utilities_test.cpp
)

add_executable(polyline_converter_test
	tests/polyline_converter_test.cpp
)

add_executable(mctx_path_test
	tests/mctx_path_test.cpp
	${src}
	${mctx_path_src}
)

if (HAVE_BOOST_MULTIPRECISION)
	add_executable(numeric_utilities_test
		tests/numeric_utilities_test.cpp
	)
endif ()

if (BUILD_TESTING)
	add_test(NAME mctx_test COMMAND mctx_test)
	add_test(NAME mctx_raw_stress_test COMMAND mctx_raw_stress_test)
	add_test(NAME collected_utilities_test COMMAND collected_utilities_test)
	add_test(NAME buffered_queue_spsc_test COMMAND buffered_queue_spsc_test)
	add_test(NAME buffered_block_list_test COMMAND buffered_block_list_test)
	add_test(NAME buffered_file_reader_test COMMAND buffered_file_reader_test)
	add_test(NAME matrix_utilities_test COMMAND matrix_utilities_test)
	add_test(NAME polyline_converter_test COMMAND polyline_converter_test)
	add_test(NAME mctx_path_test COMMAND mctx_path_test)
	if (HAVE_BOOST_MULTIPRECISION)
		add_test(NAME numeric_utilities_test COMMAND numeric_utilities_test)
	endif ()
endif ()
