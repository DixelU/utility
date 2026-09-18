#pragma once

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#define DIXELU_BUFFERED_READER_FORCE_INLINE __forceinline
#else
#define DIXELU_BUFFERED_READER_FORCE_INLINE inline
#endif

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
	~buffered_file_reader()
	{
		close();
	}

	buffered_file_reader(buffered_file_reader&& other) noexcept :
		file_(std::exchange(other.file_, nullptr)),
		buffer_(std::move(other.buffer_)),
		buffer_begin_(other.buffer_begin_),
		buffer_end_(other.buffer_end_),
		position_(other.position_),
		size_(other.size_),
		open_(other.open_),
		failed_(other.failed_),
		last_error_(other.last_error_)
	{
		other.reset_after_move();
	}

	buffered_file_reader& operator=(buffered_file_reader&& other) noexcept
	{
		if (this == &other)
			return *this;

		close();
		file_ = std::exchange(other.file_, nullptr);
		buffer_ = std::move(other.buffer_);
		buffer_begin_ = other.buffer_begin_;
		buffer_end_ = other.buffer_end_;
		position_ = other.position_;
		size_ = other.size_;
		open_ = other.open_;
		failed_ = other.failed_;
		last_error_ = other.last_error_;
		other.reset_after_move();
		return *this;
	}

	bool open(const std::filesystem::path& path)
	{
		close();
		errno = 0;
#if defined(_MSC_VER)
		const errno_t open_error = _wfopen_s(&file_, path.c_str(), L"rb");
#elif defined(_WIN32)
		file_ = _wfopen(path.c_str(), L"rb");
		const int open_error = file_ ? 0 : errno;
#else
		file_ = std::fopen(path.c_str(), "rb");
		const int open_error = file_ ? 0 : errno;
#endif
		if (open_error || !file_)
		{
			last_error_ = open_error
				? std::error_code(open_error, std::generic_category())
				: std::make_error_code(std::errc::no_such_file_or_directory);
			return false;
		}
		open_ = true;

		std::error_code size_error;
		const std::uintmax_t file_size = std::filesystem::file_size(path, size_error);
		if (size_error)
			return fail_open(size_error);
		if (file_size > std::numeric_limits<size_type>::max())
			return fail_open(std::make_error_code(std::errc::file_too_large));

		size_ = static_cast<size_type>(file_size);
		return true;
	}

	bool reopen(const std::filesystem::path& path)
	{
		return open(path);
	}

	void close()
	{
		if (file_)
			std::fclose(file_);
		file_ = nullptr;
		buffer_begin_ = 0;
		buffer_end_ = 0;
		position_ = 0;
		size_ = 0;
		open_ = false;
		failed_ = false;
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

	[[nodiscard]] DIXELU_BUFFERED_READER_FORCE_INLINE bool good() const noexcept
	{
		return is_open() && position_ < size_ && !failed_;
	}

	[[nodiscard]] DIXELU_BUFFERED_READER_FORCE_INLINE bool eof() const noexcept
	{
		return is_open() && position_ == size_;
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

#if defined(_WIN32)
		const int seek_error = _fseeki64(file_, static_cast<__int64>(absolute_position), SEEK_SET);
#else
		if (absolute_position > static_cast<size_type>(std::numeric_limits<long>::max()))
			return false;
		const int seek_error = std::fseek(file_, static_cast<long>(absolute_position), SEEK_SET);
#endif
		if (seek_error != 0)
		{
			failed_ = true;
			last_error_ = errno
				? std::error_code(errno, std::generic_category())
				: std::make_error_code(std::errc::io_error);
			return false;
		}

		buffer_begin_ = 0;
		buffer_end_ = 0;
		position_ = absolute_position;
		failed_ = false;
		last_error_.clear();
		return true;
	}

	[[nodiscard]] DIXELU_BUFFERED_READER_FORCE_INLINE std::optional<std::byte> get()
	{
		if (buffer_begin_ == buffer_end_ && !fill_buffer())
			return std::nullopt;

		return take_buffered_byte();
	}

	// Bounds-safe byte access for protocols with a defined short-read value.
	// Unlike get(), this avoids materializing an optional on byte-at-a-time hot
	// paths. failed() and eof() still distinguish the reason for the fallback.
	[[nodiscard]] DIXELU_BUFFERED_READER_FORCE_INLINE std::byte get_or(std::byte short_read_value)
	{
		if (buffer_begin_ == buffer_end_ && !fill_buffer())
			return short_read_value;

		return take_buffered_byte();
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

		return position_ - start;
	}

private:
	DIXELU_BUFFERED_READER_FORCE_INLINE std::byte take_buffered_byte() noexcept
	{
		const std::byte value = buffer_[buffer_begin_++];
		++position_;
		return value;
	}

	static std::size_t checked_buffer_capacity(std::size_t capacity)
	{
		if (capacity == 0 || capacity > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
			throw std::invalid_argument("buffered_file_reader capacity must fit in streamsize and be nonzero");
		return capacity;
	}

	bool fail_open(std::error_code error)
	{
		if (file_)
			std::fclose(file_);
		file_ = nullptr;
		buffer_begin_ = 0;
		buffer_end_ = 0;
		position_ = 0;
		size_ = 0;
		open_ = false;
		failed_ = false;
		last_error_ = error;
		return false;
	}

	bool fill_buffer()
	{
		if (!is_open() || failed_ || position_ >= size_)
			return false;

		const size_type remaining_size = size_ - position_;
		const std::size_t requested = static_cast<std::size_t>(
			std::min<size_type>(remaining_size, buffer_.size()));
		errno = 0;
#if defined(_MSC_VER)
		const std::size_t read_count = _fread_nolock_s(
			buffer_.data(), buffer_.size(), 1, requested, file_);
#else
		const std::size_t read_count = std::fread(buffer_.data(), 1, requested, file_);
#endif
		if (read_count <= 0)
		{
			failed_ = true;
			last_error_ = errno
				? std::error_code(errno, std::generic_category())
				: std::make_error_code(std::errc::io_error);
			return false;
		}

		buffer_begin_ = 0;
		buffer_end_ = read_count;
		if (buffer_end_ < requested)
		{
			// The file was truncated after opening. Preserve the bytes read and
			// make the newly observed end the logical EOF.
			size_ = position_ + buffer_end_;
			clearerr(file_);
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
		open_ = false;
		failed_ = false;
		last_error_.clear();
	}

	std::FILE* file_ = nullptr;
	std::vector<std::byte> buffer_;
	std::size_t buffer_begin_ = 0;
	std::size_t buffer_end_ = 0;
	size_type position_ = 0;
	size_type size_ = 0;
	bool open_ = false;
	bool failed_ = false;
	std::error_code last_error_;
};

} // namespace dixelu

#undef DIXELU_BUFFERED_READER_FORCE_INLINE
