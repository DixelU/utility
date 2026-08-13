#include <buffered_file_reader.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{

void require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

struct temporary_files
{
	std::filesystem::path data;
	std::filesystem::path empty;
	std::filesystem::path missing;

	temporary_files(
		std::filesystem::path data_path,
		std::filesystem::path empty_path,
		std::filesystem::path missing_path) :
		data(std::move(data_path)),
		empty(std::move(empty_path)),
		missing(std::move(missing_path))
	{}

	temporary_files(const temporary_files&) = delete;
	temporary_files& operator=(const temporary_files&) = delete;
	temporary_files(temporary_files&&) = default;
	temporary_files& operator=(temporary_files&&) = default;

	~temporary_files()
	{
		std::error_code ignored;
		std::filesystem::remove(data, ignored);
		std::filesystem::remove(empty, ignored);
	}
};

temporary_files make_test_files()
{
	const auto nonce = std::to_string(
		std::chrono::steady_clock::now().time_since_epoch().count());
	const auto base = std::filesystem::temp_directory_path() /
		std::filesystem::u8path(u8"dixelu_buffered_reader_файл_");
	temporary_files files{
		base.native() + std::filesystem::path(nonce + ".bin").native(),
		base.native() + std::filesystem::path(nonce + "_empty.bin").native(),
		base.native() + std::filesystem::path(nonce + "_missing.bin").native()};

	const std::array<unsigned char, 9> bytes{0, 1, 2, 3, 4, 5, 6, 7, 255};
	std::ofstream data(files.data, std::ios::binary);
	data.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	require(static_cast<bool>(data), "failed to create buffered reader test data");
	data.close();

	std::ofstream empty(files.empty, std::ios::binary);
	require(static_cast<bool>(empty), "failed to create empty buffered reader test data");
	return files;
}

void run_tests()
{
	bool rejected_zero_capacity = false;
	try
	{
		dixelu::buffered_file_reader invalid(0);
	}
	catch (const std::invalid_argument&)
	{
		rejected_zero_capacity = true;
	}
	require(rejected_zero_capacity, "zero buffer capacity must be rejected");

	auto files = make_test_files();
	dixelu::buffered_file_reader reader(files.data, 4);
	require(reader.is_open(), "reader must open a Unicode path");
	require(reader.size() == 9 && reader.remaining() == 9, "reader must report its size");

	for (unsigned int expected = 0; expected < 8; ++expected)
	{
		const auto value = reader.get();
		require(value.has_value(), "reader ended before the last byte");
		require(std::to_integer<unsigned int>(*value) == expected, "reader changed byte data");
	}
	require(reader.get_or(std::byte{0}) == std::byte{255}, "get_or must return a real byte");
	require(reader.eof() && !reader.good(), "last byte must establish EOF");
	require(!reader.get(), "get must represent EOF separately from byte zero");
	require(reader.get_or(std::byte{42}) == std::byte{42}, "get_or must use its EOF fallback");
	require(reader.position() == reader.size(), "EOF reads must not advance position");

	require(reader.seek(2), "seek within the file must succeed");
	std::array<std::byte, 4> bulk{};
	require(reader.read(bulk) == bulk.size(), "bulk read must fill its destination");
	require(bulk.front() == std::byte{2} && bulk.back() == std::byte{5}, "bulk read changed order");
	require(!reader.seek(10) && reader.position() == 6, "invalid seek must preserve position");

	require(reader.seek(6), "seek before copy_to must succeed");
	std::ostringstream copied;
	require(reader.copy_to(copied) == 3, "copy_to must report consumed bytes");
	require(copied.str() == std::string("\x06\x07\xff", 3), "copy_to changed byte data");
	require(reader.is_open() && reader.eof(), "copy_to must leave the file open at EOF");

	require(reader.reopen(files.empty), "empty file must open");
	require(reader.is_open() && reader.size() == 0 && reader.eof(), "empty file state is invalid");
	require(!reader.reopen(files.missing), "missing file must fail to open");
	require(!reader.is_open() && reader.last_error(), "failed reopen must retain an error");
	require(reader.open(files.data), "reader must recover after failed reopen");

	dixelu::buffered_file_reader moved(std::move(reader));
	require(!reader.is_open() && moved.is_open(), "move construction must transfer ownership");
	dixelu::buffered_file_reader assigned;
	assigned = std::move(moved);
	require(!moved.is_open() && assigned.is_open(), "move assignment must transfer ownership");
	assigned.close();
	assigned.close();
	require(!assigned.is_open(), "close must be idempotent");

	{
		dixelu::buffered_file_reader scoped(files.data);
		require(scoped.is_open(), "scoped reader must open test data");
	}
	std::error_code remove_error;
	require(
		std::filesystem::remove(files.data, remove_error) && !remove_error,
		"reader destruction must release its file handle");
}

} // namespace

int main()
{
	try
	{
		run_tests();
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
