#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace dixelu
{

#ifdef _WIN32
using native_char = wchar_t;
#else
using native_char = char;
#endif

using native_string = std::basic_string<native_char>;
using native_string_view = std::basic_string_view<native_char>;

template<std::size_t N>
struct native_literal
{
	std::array<native_char, N> value{};

	[[nodiscard]] constexpr native_string_view view() const noexcept
	{
		return {value.data(), N == 0 ? 0 : N - 1};
	}

	[[nodiscard]] native_string str() const
	{
		return native_string(view());
	}

	constexpr operator native_string_view() const noexcept { return view(); }
	explicit operator native_string() const { return str(); }
};

// Converts an ASCII source literal to the platform's native narrow/wide type.
// This deliberately does not attempt runtime UTF-8 transcoding.
template<std::size_t N>
consteval native_literal<N> to_native_literal(const char (&text)[N])
{
	native_literal<N> result;
	for (std::size_t i = 0; i < N; ++i)
		result.value[i] = static_cast<native_char>(static_cast<unsigned char>(text[i]));
	return result;
}

} // namespace dixelu
