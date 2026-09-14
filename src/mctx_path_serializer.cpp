// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Alexander Verevkin
#include "mctx_path_serializer.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace dixelu
{
namespace
{

class path_parser
{
public:
	explicit path_parser(std::string_view text): _text(text) {}

	[[nodiscard]] mctx_path parse()
	{
		if(_text.empty())
			return {};
		auto result = parse_path({}, 0);
		if(!at_end())
			fail("unexpected trailing input");
		return result;
	}

private:
	[[nodiscard]] bool at_end() const noexcept { return _position == _text.size(); }
	[[nodiscard]] char peek() const noexcept { return at_end() ? '\0' : _text[_position]; }

	[[noreturn]] void fail(const char* message) const
	{
		throw std::invalid_argument(
			"mctx_path parse error at byte " + std::to_string(_position) + ": " + message);
	}

	[[nodiscard]] bool starts_with(std::string_view value) const noexcept
	{
		return _text.substr(_position, value.size()) == value;
	}

	bool consume(char value) noexcept
	{
		if(peek() != value)
			return false;
		++_position;
		return true;
	}

	bool consume(std::string_view value) noexcept
	{
		if(!starts_with(value))
			return false;
		_position += value.size();
		return true;
	}

	[[nodiscard]] static bool is_stop(char value, std::string_view stops) noexcept
	{
		return value != '\0' && stops.find(value) != std::string_view::npos;
	}

	[[nodiscard]] mctx_path parse_path(std::string_view stops, std::size_t depth)
	{
		if(depth > 16)
			fail("query nesting is too deep");

		mctx_path result;
		bool parsed_segment = false;
		for(;;)
		{
			if(at_end() || is_stop(peek(), stops))
				break;
			if(parsed_segment)
			{
				if(!consume('/'))
					fail("expected '/' between path segments");
				if(at_end() || is_stop(peek(), stops) || peek() == '/')
					fail("empty path segment");
			}

			result = std::move(result) / parse_segment(depth);
			parsed_segment = true;
		}
		return result;
	}

	[[nodiscard]] mctx_path parse_segment(std::size_t depth)
	{
		if(consume('#'))
			return mctx_path(mctx_path::root);
		if(consume(".."))
			return mctx_path(mctx_path::previous);
		if(consume('*'))
		{
			if(consume(":{"))
				return parse_filter(depth + 1);
			return mctx_path(mctx_path::variable);
		}
		if(peek() == '"')
			return mctx_path(parse_quoted_string());
		if(peek() == '-' && _position + 1 < _text.size() &&
			std::isdigit(static_cast<unsigned char>(_text[_position + 1])) != 0)
		{
			fail("array indices cannot be negative");
		}
		if(std::isdigit(static_cast<unsigned char>(peek())) != 0)
			return mctx_path(parse_index());

		const auto begin = _position;
		while(!at_end())
		{
			const char value = peek();
			if(value == '/' || value == '=' || value == '&' || value == '}' ||
				value == '{' || value == ':' || value == '"' || value == '#')
			{
				break;
			}
			++_position;
		}
		if(begin == _position)
			fail("unexpected character in path segment");

		auto value = std::string(_text.substr(begin, _position - begin));
		while(!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
			value.pop_back();
		std::size_t first = 0;
		while(first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0)
			++first;
		value.erase(0, first);
		if(value.empty())
			fail("unquoted key is empty");
		return mctx_path(std::move(value));
	}

	[[nodiscard]] mctx_path parse_filter(std::size_t depth)
	{
		std::vector<mctx_path::query> queries;
		for(;;)
		{
			if(consume('}'))
			{
				if(queries.empty())
					fail("query set cannot be empty");
				break;
			}

			// The archived canonical form prefixes each relative query with "*/".
			consume("*/");
			auto relative_path = parse_path("=", depth);
			if(relative_path.empty())
				fail("query path cannot be empty");
			if(!consume('='))
				fail("expected '=' after query path");

			if(consume('*'))
				queries.push_back(mctx_path::query_exists(std::move(relative_path)));
			else
				queries.push_back(mctx_path::query_equals(
					std::move(relative_path), parse_expected_value()));

			if(consume('&'))
				continue;
			if(consume('}'))
				break;
			fail("expected '&' or '}' after query value");
		}
		return mctx_path::where_all(std::move(queries));
	}

	[[nodiscard]] std::string parse_quoted_string()
	{
		if(!consume('"'))
			fail("expected quoted string");
		std::string result;
		while(!at_end())
		{
			const char value = _text[_position++];
			if(value == '"')
				return result;
			if(value != '\\')
			{
				result.push_back(value);
				continue;
			}
			if(at_end())
				fail("unterminated escape sequence");
			switch(_text[_position++])
			{
				case '\\': result.push_back('\\'); break;
				case '"': result.push_back('"'); break;
				case 'n': result.push_back('\n'); break;
				case 'r': result.push_back('\r'); break;
				case 't': result.push_back('\t'); break;
				default: fail("unsupported string escape");
			}
		}
		fail("unterminated quoted string");
	}

	[[nodiscard]] std::size_t parse_index()
	{
		const auto begin = _position;
		while(std::isdigit(static_cast<unsigned char>(peek())) != 0)
			++_position;
		std::uint64_t value = 0;
		const auto token = _text.substr(begin, _position - begin);
		const auto conversion = std::from_chars(token.data(), token.data() + token.size(), value);
		if(conversion.ec != std::errc{} || conversion.ptr != token.data() + token.size() ||
			value > std::numeric_limits<std::size_t>::max())
		{
			fail("array index is out of range");
		}
		return static_cast<std::size_t>(value);
	}

	[[nodiscard]] mctx parse_expected_value()
	{
		if(peek() == '"')
			return mctx(parse_quoted_string());

		const auto begin = _position;
		while(!at_end() && peek() != '&' && peek() != '}')
			++_position;
		if(begin == _position)
			fail("query value cannot be empty");

		auto token = _text.substr(begin, _position - begin);
		while(!token.empty() && std::isspace(static_cast<unsigned char>(token.front())) != 0)
			token.remove_prefix(1);
		while(!token.empty() && std::isspace(static_cast<unsigned char>(token.back())) != 0)
			token.remove_suffix(1);
		if(token.empty())
			fail("query value cannot be empty");
		if(token == "null")
			return {};
		if(token == "true")
			return mctx(true);
		if(token == "false")
			return mctx(false);

		const bool negative = token.front() == '-';
		const auto digits = negative ? token.substr(1) : token;
		const bool all_digits = !digits.empty() &&
			std::ranges::all_of(digits, [](const char value)
			{
				return std::isdigit(static_cast<unsigned char>(value)) != 0;
			});
		if(all_digits)
		{
			if(negative)
			{
				std::int64_t value = 0;
				const auto conversion = std::from_chars(
					token.data(), token.data() + token.size(), value);
				if(conversion.ec != std::errc{} || conversion.ptr != token.data() + token.size())
					fail("signed query integer is out of range");
				return mctx(value);
			}

			std::uint64_t value = 0;
			const auto conversion = std::from_chars(
				token.data(), token.data() + token.size(), value);
			if(conversion.ec != std::errc{} || conversion.ptr != token.data() + token.size())
				fail("unsigned query integer is out of range");
			return mctx(value);
		}

		// Preserve the archived parser's acceptance of unquoted string values.
		return mctx(std::string(token));
	}

	std::string_view _text;
	std::size_t _position = 0;
};

} // namespace

mctx_path mctx_path_serializer::deserialize(std::string_view text)
{
	return path_parser(text).parse();
}

namespace literals
{

mctx_path operator""_mctx_path(const char* text, std::size_t size)
{
	return mctx_path_serializer::deserialize(std::string_view(text, size));
}

} // namespace literals
} // namespace dixelu
