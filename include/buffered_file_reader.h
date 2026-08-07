#pragma once

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace dixelu
{

// A binary, read-only file with an explicit user-space read buffer.
//
// EOF is represented separately from byte value zero. The reader owns its file
// until close(), destruction, or a successful/failed reopen. copy_to() consumes
// the remaining bytes but deliberately leaves the file open.
class buffered_file_reader
{
public:
	using size_type = std::uint64_t;
	static constexpr std::size_t default_buffer_capacity = 64 * 1024;

	explicit buffered_file_reader(
		std::size_t buffer_capacity = default_buffer_capacity) :
		buffer_(checked_buffer_capacity(buffer_capacity))
	{}

	buffered_file_reader(
		const std::filesystem::path& path,
		std::size_t buffer_capacity = default_buffer_capacity) :
		buffered_file_reader(buffer_capacity)
	{
		open(path);
	}

	buffered_file_reader(const buffered_file_reader&) = delete;
	buffered_file_reader& operator=(const buffered_file_reader&) = delete;

	buffered_file_reader(buffered_file_reader&& other) :
		stream_(std::move(other.stream_)),
		buffer_(std::move(other.buffer_)),
		buffer_begin_(other.buffer_begin_),
		buffer_end_(other.buffer_end_),
		position_(other.position_),
		size_(other.size_),
		eof_(other.eof_),
		failed_(other.failed_),
		last_error_(other.last_error_)
	{
		other.reset_after_move();
	}

	buffered_file_reader& operator=(buffered_file_reader&& other)
	{
		if (this == &other)
			return *this;

		close();
		stream_ = std::move(other.stream_);
		buffer_ = std::move(other.buffer_);
		buffer_begin_ = other.buffer_begin_;
		buffer_end_ = other.buffer_end_;
		position_ = other.position_;
		size_ = other.size_;
		eof_ = other.eof_;
		failed_ = other.failed_;
		last_error_ = other.last_error_;
		other.reset_after_move();
		return *this;
	}

	bool open(const std::filesystem::path& path)
	{
		close();
		errno = 0;
		stream_.open(path, std::ios::binary | std::ios::in);
		if (!stream_.is_open())
		{
			last_error_ = errno
				? std::error_code(errno, std::generic_category())
				: std::make_error_code(std::errc::no_such_file_or_directory);
			return false;
		}

		stream_.seekg(0, std::ios::end);
		const std::streampos end = stream_.tellg();
		if (end < std::streampos(0))
			return fail_open(std::make_error_code(std::errc::io_error));

		const auto end_offset = static_cast<std::uintmax_t>(end);
		if (end_offset > std::numeric_limits<size_type>::max())
			return fail_open(std::make_error_code(std::errc::file_too_large));

		stream_.seekg(0, std::ios::beg);
		if (!stream_)
			return fail_open(std::make_error_code(std::errc::io_error));

		size_ = static_cast<size_type>(end_offset);
		eof_ = size_ == 0;
		return true;
	}

	bool reopen(const std::filesystem::path& path)
	{
		return open(path);
	}

	void close()
	{
		if (stream_.is_open())
			stream_.close();
		stream_.clear();
		buffer_begin_ = 0;
		buffer_end_ = 0;
		position_ = 0;
		size_ = 0;
		eof_ = false;
		failed_ = false;
		last_error_.clear();
	}

	[[nodiscard]] bool is_open() const noexcept
	{
		return stream_.is_open();
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return is_open();
	}

	[[nodiscard]] bool good() const noexcept
	{
		return is_open() && !eof_ && !failed_;
	}

	[[nodiscard]] bool eof() const noexcept
	{
		return is_open() && eof_;
	}

	[[nodiscard]] bool failed() const noexcept
	{
		return failed_;
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

	[[nodiscard]] std::size_t buffer_capacity() const noexcept
	{
		return buffer_.size();
	}

	bool seek(size_type absolute_position)
	{
		if (!is_open() || absolute_position > size_)
			return false;

		stream_.clear();
		stream_.seekg(static_cast<std::streamoff>(absolute_position), std::ios::beg);
		if (!stream_)
		{
			failed_ = true;
			last_error_ = std::make_error_code(std::errc::io_error);
			return false;
		}

		buffer_begin_ = 0;
		buffer_end_ = 0;
		position_ = absolute_position;
		eof_ = position_ == size_;
		failed_ = false;
		last_error_.clear();
		return true;
	}

	[[nodiscard]] std::optional<std::byte> get()
	{
		std::byte value{};
		return read(std::span<std::byte>(&value, 1)) == 1
			? std::optional<std::byte>(value)
			: std::nullopt;
	}

	std::size_t read(std::span<std::byte> destination)
	{
		std::size_t total = 0;
		while (total < destination.size())
		{
			if (buffer_begin_ == buffer_end_ && !fill_buffer())
				break;

			const std::size_t available = buffer_end_ - buffer_begin_;
			const std::size_t count = std::min(available, destination.size() - total);
			std::memcpy(destination.data() + total, buffer_.data() + buffer_begin_, count);
			buffer_begin_ += count;
			position_ += count;
			total += count;
		}

		if (is_open() && position_ == size_)
			eof_ = true;
		return total;
	}

	// Consumes the remaining input, but does not close the owned file.
	size_type copy_to(std::ostream& output)
	{
		const size_type start = position_;
		while (output && position_ < size_)
		{
			if (buffer_begin_ == buffer_end_ && !fill_buffer())
				break;

			const std::size_t count = buffer_end_ - buffer_begin_;
			output.write(
				reinterpret_cast<const char*>(buffer_.data() + buffer_begin_),
				static_cast<std::streamsize>(count));
			if (!output)
				break;

			buffer_begin_ += count;
			position_ += count;
		}

		if (is_open() && position_ == size_)
			eof_ = true;
		return position_ - start;
	}

private:
	static std::size_t checked_buffer_capacity(std::size_t capacity)
	{
		if (capacity == 0 || capacity > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
			throw std::invalid_argument("buffered_file_reader capacity must fit in streamsize and be nonzero");
		return capacity;
	}

	bool fail_open(std::error_code error)
	{
		if (stream_.is_open())
			stream_.close();
		stream_.clear();
		buffer_begin_ = 0;
		buffer_end_ = 0;
		position_ = 0;
		size_ = 0;
		eof_ = false;
		failed_ = false;
		last_error_ = error;
		return false;
	}

	bool fill_buffer()
	{
		if (!is_open() || failed_ || position_ >= size_)
		{
			if (is_open() && position_ >= size_)
				eof_ = true;
			return false;
		}

		const size_type remaining_size = size_ - position_;
		const std::size_t requested = static_cast<std::size_t>(
			std::min<size_type>(remaining_size, buffer_.size()));
		stream_.read(reinterpret_cast<char*>(buffer_.data()), static_cast<std::streamsize>(requested));
		const std::streamsize read_count = stream_.gcount();
		if (read_count <= 0)
		{
			failed_ = true;
			last_error_ = std::make_error_code(std::errc::io_error);
			return false;
		}

		buffer_begin_ = 0;
		buffer_end_ = static_cast<std::size_t>(read_count);
		if (buffer_end_ < requested)
		{
			// The file was truncated after opening. Preserve the bytes read and
			// make the newly observed end the logical EOF.
			size_ = position_ + buffer_end_;
			stream_.clear();
		}
		return true;
	}

	void reset_after_move()
	{
		if (buffer_.empty())
			buffer_.resize(default_buffer_capacity);
		buffer_begin_ = 0;
		buffer_end_ = 0;
		position_ = 0;
		size_ = 0;
		eof_ = false;
		failed_ = false;
		last_error_.clear();
	}

	std::ifstream stream_;
	std::vector<std::byte> buffer_;
	std::size_t buffer_begin_ = 0;
	std::size_t buffer_end_ = 0;
	size_type position_ = 0;
	size_type size_ = 0;
	bool eof_ = false;
	bool failed_ = false;
	std::error_code last_error_;
};

} // namespace dixelu
