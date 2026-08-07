#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace dixelu
{

// Strict RFC 4648 Base64 decoder. ASCII whitespace is ignored; malformed
// length, padding, alphabet, or non-canonical trailing bits are rejected.
inline std::vector<std::uint8_t> base64_decode(std::string_view encoded)
{
	auto decode_digit = [](unsigned char value) -> int {
		if (value >= 'A' && value <= 'Z') return value - 'A';
		if (value >= 'a' && value <= 'z') return value - 'a' + 26;
		if (value >= '0' && value <= '9') return value - '0' + 52;
		if (value == '+') return 62;
		if (value == '/') return 63;
		return -1;
	};

	std::vector<std::uint8_t> result;
	result.reserve(encoded.size() / 4 * 3);
	std::array<int, 4> quartet{};
	std::size_t quartet_size = 0;
	bool finished = false;

	for (const char raw_character : encoded)
	{
		const auto character = static_cast<unsigned char>(raw_character);
		if (std::isspace(character))
			continue;
		if (finished)
			throw std::invalid_argument("Base64 data follows final padding");

		if (character == '=')
			quartet[quartet_size++] = -2;
		else
		{
			const int digit = decode_digit(character);
			if (digit < 0)
				throw std::invalid_argument("Invalid Base64 character");
			quartet[quartet_size++] = digit;
		}

		if (quartet_size != quartet.size())
			continue;

		if (quartet[0] < 0 || quartet[1] < 0)
			throw std::invalid_argument("Invalid Base64 padding");

		result.push_back(static_cast<std::uint8_t>(
			(quartet[0] << 2) | (quartet[1] >> 4)));

		if (quartet[2] == -2)
		{
			if (quartet[3] != -2 || (quartet[1] & 0x0F) != 0)
				throw std::invalid_argument("Invalid Base64 padding bits");
			finished = true;
		}
		else
		{
			if (quartet[2] < 0)
				throw std::invalid_argument("Invalid Base64 padding");
			result.push_back(static_cast<std::uint8_t>(
				((quartet[1] & 0x0F) << 4) | (quartet[2] >> 2)));

			if (quartet[3] == -2)
			{
				if ((quartet[2] & 0x03) != 0)
					throw std::invalid_argument("Invalid Base64 padding bits");
				finished = true;
			}
			else
			{
				if (quartet[3] < 0)
					throw std::invalid_argument("Invalid Base64 padding");
				result.push_back(static_cast<std::uint8_t>(
					((quartet[2] & 0x03) << 6) | quartet[3]));
			}
		}

		quartet_size = 0;
	}

	if (quartet_size != 0)
		throw std::invalid_argument("Truncated Base64 input");
	return result;
}

} // namespace dixelu
