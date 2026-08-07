#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace dixelu
{

// Fixed-capacity, null-terminated string. Assignments longer than Capacity are
// deliberately truncated, matching the behavior of the collected oss_string.
template<std::size_t Capacity = 30>
class fixed_string
{
	std::array<char, Capacity + 1> buffer_{};
	std::size_t size_ = 0;

public:
	constexpr fixed_string() noexcept = default;

	constexpr fixed_string(std::string_view value) noexcept
	{
		assign(value);
	}

	constexpr fixed_string(const char* value) noexcept :
		fixed_string(value ? std::string_view(value) : std::string_view())
	{}

	constexpr void assign(std::string_view value) noexcept
	{
		size_ = std::min(value.size(), Capacity);
		std::copy_n(value.data(), size_, buffer_.data());
		buffer_[size_] = '\0';
	}

	[[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
	[[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
	[[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
	[[nodiscard]] constexpr const char* c_str() const noexcept { return buffer_.data(); }
	[[nodiscard]] constexpr const char* data() const noexcept { return buffer_.data(); }
	[[nodiscard]] constexpr char* data() noexcept { return buffer_.data(); }

	[[nodiscard]] constexpr const char* begin() const noexcept { return buffer_.data(); }
	[[nodiscard]] constexpr const char* end() const noexcept { return buffer_.data() + size_; }
	[[nodiscard]] constexpr char* begin() noexcept { return buffer_.data(); }
	[[nodiscard]] constexpr char* end() noexcept { return buffer_.data() + size_; }

	[[nodiscard]] constexpr std::string_view view() const noexcept
	{
		return {buffer_.data(), size_};
	}

	[[nodiscard]] std::string to_string() const
	{
		return std::string(view());
	}

	explicit operator std::string() const { return to_string(); }
	explicit constexpr operator std::string_view() const noexcept { return view(); }

	friend constexpr bool operator==(const fixed_string&, const fixed_string&) = default;
	friend constexpr auto operator<=>(const fixed_string& lhs, const fixed_string& rhs) noexcept
	{
		return lhs.view() <=> rhs.view();
	}
};

using fixed_string_62 = fixed_string<62>;
using fixed_string_30 = fixed_string<30>;
using fixed_string_14 = fixed_string<14>;
using fixed_string_6 = fixed_string<6>;

} // namespace dixelu

namespace std
{

template<std::size_t Capacity>
struct hash<dixelu::fixed_string<Capacity>>
{
	std::size_t operator()(const dixelu::fixed_string<Capacity>& value) const noexcept
	{
		return std::hash<std::string_view>{}(value.view());
	}
};

} // namespace std
