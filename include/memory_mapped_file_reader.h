#pragma once

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#define DIXELU_MEMORY_MAPPED_READER_DEFINED_NOMINMAX
#endif
#include <windows.h>
#ifdef DIXELU_MEMORY_MAPPED_READER_DEFINED_NOMINMAX
#undef NOMINMAX
#undef DIXELU_MEMORY_MAPPED_READER_DEFINED_NOMINMAX
#endif
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace dixelu
{

// A read-only whole-file mapping with cursor-style access.
//
// Empty files are valid open mappings with no data pointer. The class has sole
// ownership of every native handle; ownership transfers on move and close() is
// idempotent.
class memory_mapped_file_reader
{
public:
	using size_type = std::uint64_t;

	memory_mapped_file_reader() = default;

	explicit memory_mapped_file_reader(const std::filesystem::path& path)
	{
		open(path);
	}

	~memory_mapped_file_reader()
	{
		release_resources();
	}

	memory_mapped_file_reader(const memory_mapped_file_reader&) = delete;
	memory_mapped_file_reader& operator=(const memory_mapped_file_reader&) = delete;

	memory_mapped_file_reader(memory_mapped_file_reader&& other) noexcept
	{
		move_from(other);
	}

	memory_mapped_file_reader& operator=(memory_mapped_file_reader&& other) noexcept
	{
		if (this != &other)
		{
			release_resources();
			move_from(other);
		}
		return *this;
	}

	bool open(const std::filesystem::path& path)
	{
		close();

#ifdef _WIN32
		const std::wstring native_path = path.wstring();
		file_ = CreateFileW(
			native_path.c_str(),
			GENERIC_READ,
			FILE_SHARE_READ,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file_ == INVALID_HANDLE_VALUE)
			return fail_open(std::error_code(static_cast<int>(GetLastError()), std::system_category()));

		LARGE_INTEGER file_size{};
		if (!GetFileSizeEx(file_, &file_size) || file_size.QuadPart < 0)
			return fail_open(std::error_code(static_cast<int>(GetLastError()), std::system_category()));

		const auto unsigned_size = static_cast<unsigned long long>(file_size.QuadPart);
		if (unsigned_size > std::numeric_limits<std::size_t>::max())
			return fail_open(std::make_error_code(std::errc::file_too_large));

		size_ = static_cast<size_type>(unsigned_size);
		open_ = true;
		if (size_ == 0)
			return true;

		mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
		if (mapping_ == nullptr)
			return fail_open(std::error_code(static_cast<int>(GetLastError()), std::system_category()));

		void* view = MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0);
		if (view == nullptr)
			return fail_open(std::error_code(static_cast<int>(GetLastError()), std::system_category()));
		data_ = static_cast<const std::byte*>(view);
#else
		file_ = ::open(path.c_str(), O_RDONLY);
		if (file_ == -1)
			return fail_open(std::error_code(errno, std::generic_category()));

		struct stat file_status{};
		if (::fstat(file_, &file_status) != 0 || file_status.st_size < 0)
			return fail_open(std::error_code(errno, std::generic_category()));

		const auto unsigned_size = static_cast<std::uintmax_t>(file_status.st_size);
		if (unsigned_size > std::numeric_limits<std::size_t>::max())
			return fail_open(std::make_error_code(std::errc::file_too_large));

		size_ = static_cast<size_type>(unsigned_size);
		open_ = true;
		if (size_ == 0)
			return true;

		void* view = ::mmap(nullptr, static_cast<std::size_t>(size_), PROT_READ, MAP_PRIVATE, file_, 0);
		if (view == MAP_FAILED)
			return fail_open(std::error_code(errno, std::generic_category()));
		data_ = static_cast<const std::byte*>(view);
#endif

		return true;
	}

	bool reopen(const std::filesystem::path& path)
	{
		return open(path);
	}

	void close() noexcept
	{
		release_resources();
		last_error_.clear();
	}

	[[nodiscard]] bool is_open() const noexcept
	{
		return open_;
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return is_open();
	}

	[[nodiscard]] bool good() const noexcept
	{
		return open_ && position_ < size_;
	}

	[[nodiscard]] bool eof() const noexcept
	{
		return open_ && position_ == size_;
	}

	[[nodiscard]] std::error_code last_error() const noexcept
	{
		return last_error_;
	}

	[[nodiscard]] size_type position() const noexcept
	{
		return position_;
	}

	[[nodiscard]] size_type size() const noexcept
	{
		return size_;
	}

	[[nodiscard]] size_type remaining() const noexcept
	{
		return position_ < size_ ? size_ - position_ : 0;
	}

	[[nodiscard]] const std::byte* data() const noexcept
	{
		return data_;
	}

	[[nodiscard]] std::span<const std::byte> bytes() const noexcept
	{
		return {data_, static_cast<std::size_t>(size_)};
	}

	[[nodiscard]] std::span<const std::byte> remaining_bytes() const noexcept
	{
		if (position_ == size_)
			return {};
		return {
			data_ + static_cast<std::size_t>(position_),
			static_cast<std::size_t>(size_ - position_)};
	}

	bool seek(size_type absolute_position) noexcept
	{
		if (!open_ || absolute_position > size_)
			return false;
		position_ = absolute_position;
		return true;
	}

	[[nodiscard]] std::optional<std::byte> get() noexcept
	{
		if (!open_ || position_ >= size_)
			return std::nullopt;
		return data_[static_cast<std::size_t>(position_++)];
	}

	std::size_t read(std::span<std::byte> destination) noexcept
	{
		if (!open_ || position_ >= size_ || destination.empty())
			return 0;

		const std::size_t count = static_cast<std::size_t>(
			std::min<size_type>(destination.size(), size_ - position_));
		std::memcpy(destination.data(), data_ + static_cast<std::size_t>(position_), count);
		position_ += count;
		return count;
	}

private:
	bool fail_open(std::error_code error) noexcept
	{
		release_resources();
		last_error_ = error ? error : std::make_error_code(std::errc::io_error);
		return false;
	}

	void release_resources() noexcept
	{
#ifdef _WIN32
		if (data_ != nullptr)
			UnmapViewOfFile(data_);
		if (mapping_ != nullptr)
			CloseHandle(mapping_);
		if (file_ != INVALID_HANDLE_VALUE)
			CloseHandle(file_);
		mapping_ = nullptr;
		file_ = INVALID_HANDLE_VALUE;
#else
		if (data_ != nullptr)
			::munmap(const_cast<std::byte*>(data_), static_cast<std::size_t>(size_));
		if (file_ != -1)
			::close(file_);
		file_ = -1;
#endif
		data_ = nullptr;
		position_ = 0;
		size_ = 0;
		open_ = false;
	}

	void move_from(memory_mapped_file_reader& other) noexcept
	{
		data_ = std::exchange(other.data_, nullptr);
		position_ = std::exchange(other.position_, 0);
		size_ = std::exchange(other.size_, 0);
		open_ = std::exchange(other.open_, false);
		last_error_ = other.last_error_;
		other.last_error_.clear();
#ifdef _WIN32
		file_ = std::exchange(other.file_, INVALID_HANDLE_VALUE);
		mapping_ = std::exchange(other.mapping_, nullptr);
#else
		file_ = std::exchange(other.file_, -1);
#endif
	}

	const std::byte* data_ = nullptr;
	size_type position_ = 0;
	size_type size_ = 0;
	bool open_ = false;
	std::error_code last_error_;
#ifdef _WIN32
	HANDLE file_ = INVALID_HANDLE_VALUE;
	HANDLE mapping_ = nullptr;
#else
	int file_ = -1;
#endif
};

} // namespace dixelu
