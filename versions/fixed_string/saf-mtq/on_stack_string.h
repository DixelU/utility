#ifndef ONSTACKSTRING_H
#define ONSTACKSTRING_H

#include <cstring>
#include <string>

namespace tdv
{

template<size_t desired_size = 32>
struct oss_string
{
	constexpr static size_t STATIC_SIZE =
		desired_size - sizeof(char) - sizeof(unsigned char);

	oss_string() = default;
	~oss_string() = default;

	oss_string(oss_string&&) = default;
	oss_string(const oss_string&) = default;

	oss_string& operator=(oss_string&& rhs) noexcept
	{
		if(&rhs == this)
			return *this;

		memcpy(this, &rhs, sizeof(oss_string));
		return *this;
	}

	oss_string& operator=(const oss_string& rhs)
	{
		if(&rhs == this)
			return *this;

		memcpy(this, &rhs, sizeof(oss_string));
		return *this;
	}

	oss_string(const char* cString)
	{
		auto stringLength = strlen(cString);
		assignFromStringBuffer(cString, stringLength);
	}

	oss_string(const std::string& string)
	{
		assignFromStringBuffer(string.c_str(), string.size());
	}

	const char* cbegin() const { return _buffer; }
	char* begin() { return _buffer; }
	const char* cend() const { return _buffer + _size; }
	char* end() { return _buffer + _size; }

	bool empty() const { return !_size; }
	size_t size() const { return _size; }

	const char* c_str() const { return _buffer; }
	const char* data() const { return _buffer; }
	size_t capacity() const { return STATIC_SIZE; }

	bool operator<(const oss_string& rhs) const
	{
		return strcmp(_buffer, rhs._buffer) < 0;
	}

	bool operator>(const oss_string& rhs) const
	{
		return strcmp(_buffer, rhs._buffer) > 0;
	}

	bool operator!=(const oss_string& rhs) const
	{
		return strcmp(_buffer, rhs._buffer) != 0;
	}

	bool operator==(const oss_string& rhs) const
	{
		return strcmp(_buffer, rhs._buffer) == 0;
	}

	explicit operator std::string() const
	{
		return std::string(cbegin(), cend());
	}

	std::string to_string() const { return static_cast<std::string>(*this); }

private:
	void assignFromStringBuffer(const char* str, size_t size)
	{
		if(size > STATIC_SIZE)
			size = STATIC_SIZE;
		memcpy(_buffer, str, size);
		_buffer[size] = 0; // the out of bounds element is _zero !!!
		_size = size;
	}

	char _buffer[STATIC_SIZE];
	char _zero{0};
	unsigned char _size{0};
};

// on stack string - 64 bytes long (capacity 62 chars)
using oss_string_64 = oss_string<64>;

// on stack string - 32 bytes long (capacity 30 chars)
using oss_string_32 = oss_string<32>;

// on stack string - 16 bytes long (capacity 14 chars)
using oss_string_16 = oss_string<16>;

// on stack string - 8 bytes long (capacity 6 chars)
using oss_string_8 = oss_string<8>;

// size is measured by the count of machine words
template<size_t machine_words>
using oss_string_bw = oss_string<(machine_words / sizeof(size_t))>;

}

namespace std
{

template <size_t desired_size>
struct hash<tdv::oss_string<desired_size>>
{
	size_t operator()(const tdv::oss_string<desired_size>& x) const noexcept
	{
		// http://www.cse.yorku.ca/~oz/hash.html
		const char* str = x.c_str();
		size_t hash = 0;
		unsigned c;

		while (c = *str++)
			hash = c + (hash << 6) + (hash << 16) - hash;

		return hash;
	}
};

}

#endif //ONSTACKSTRING_H
