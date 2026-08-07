#pragma once

#include <array>
#include <bit>
#include <charconv>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <limits>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

namespace dixelu
{

namespace long_uint_detail
{

struct wide_product
{
	std::uint64_t low;
	std::uint64_t high;
};

constexpr wide_product multiply_wide(std::uint64_t lhs, std::uint64_t rhs) noexcept
{
#if defined(__SIZEOF_INT128__) && !defined(__wasm__)
	const __uint128_t product = static_cast<__uint128_t>(lhs) * static_cast<__uint128_t>(rhs);
	return {
		static_cast<std::uint64_t>(product),
		static_cast<std::uint64_t>(product >> 64)};
#elif defined(_MSC_VER) && defined(_M_X64)
	std::uint64_t high = 0;
	const std::uint64_t low = _umul128(lhs, rhs, &high);
	return {low, high};
#else
	const std::uint64_t lhs_low = static_cast<std::uint32_t>(lhs);
	const std::uint64_t lhs_high = lhs >> 32;
	const std::uint64_t rhs_low = static_cast<std::uint32_t>(rhs);
	const std::uint64_t rhs_high = rhs >> 32;

	const std::uint64_t low_product = lhs_low * rhs_low;
	const std::uint64_t first_cross = lhs_high * rhs_low + (low_product >> 32);
	const std::uint64_t first_cross_low = static_cast<std::uint32_t>(first_cross);
	const std::uint64_t first_cross_high = first_cross >> 32;
	const std::uint64_t second_cross = lhs_low * rhs_high + first_cross_low;

	return {
		(second_cross << 32) | static_cast<std::uint32_t>(low_product),
		lhs_high * rhs_high + first_cross_high + (second_cross >> 32)};
#endif
}

} // namespace long_uint_detail

// Fixed-width unsigned integers with 2^(Degree + 1) little-endian 64-bit
// limbs: long_uint<0> is 128 bits, long_uint<1> is 256 bits, and so on.
// Arithmetic is modulo 2^bits. Division by zero throws std::domain_error and
// shifts by bits or more produce zero rather than wrapping the shift count.
template<std::uint64_t Degree>
class long_uint
{
public:
	using base_type = std::uint64_t;
	using size_type = std::size_t;
	using self_type = long_uint<Degree>;

	static_assert(Degree < std::numeric_limits<size_type>::digits - 1);
	static constexpr size_type base_bits = std::numeric_limits<base_type>::digits;
	static constexpr size_type size = size_type{2} << Degree;
	static_assert(size <= std::numeric_limits<size_type>::max() / base_bits);
	static constexpr size_type bits = size * base_bits;
	static constexpr bool is_constexpr_expensive = true;
	static_assert(bits <= static_cast<size_type>(std::numeric_limits<int>::max()));

	struct fill_fields_tag {};

	constexpr long_uint() = default;

	constexpr long_uint(base_type value) noexcept
	{
		limbs_[0] = value;
	}

	constexpr explicit long_uint(base_type value, fill_fields_tag) noexcept
	{
		limbs_.fill(value);
	}

	template<std::uint64_t OtherDegree>
	constexpr explicit long_uint(const long_uint<OtherDegree>& other) noexcept
	{
		constexpr size_type copy_count = size < long_uint<OtherDegree>::size
			? size
			: long_uint<OtherDegree>::size;
		for (size_type index = 0; index < copy_count; ++index)
			limbs_[index] = other[index];
	}

	[[nodiscard]] constexpr base_type& operator[](size_type index) noexcept
	{
		return limbs_[index];
	}

	[[nodiscard]] constexpr const base_type& operator[](size_type index) const noexcept
	{
		return limbs_[index];
	}

	[[nodiscard]] constexpr std::span<base_type, size> limbs() noexcept
	{
		return limbs_;
	}

	[[nodiscard]] constexpr std::span<const base_type, size> limbs() const noexcept
	{
		return limbs_;
	}

	[[nodiscard]] constexpr bool get_bit(size_type bit) const noexcept
	{
		return bit < bits && ((limbs_[bit / base_bits] >> (bit % base_bits)) & 1U) != 0;
	}

	constexpr bool set_bit(size_type bit, bool value = true) noexcept
	{
		if (bit >= bits)
			return false;
		const base_type mask = base_type{1} << (bit % base_bits);
		if (value)
			limbs_[bit / base_bits] |= mask;
		else
			limbs_[bit / base_bits] &= ~mask;
		return true;
	}

	[[nodiscard]] constexpr explicit operator bool() const noexcept
	{
		for (const base_type limb : limbs_)
		{
			if (limb != 0)
				return true;
		}
		return false;
	}

	[[nodiscard]] constexpr explicit operator base_type() const noexcept
	{
		return limbs_[0];
	}

	[[nodiscard]] constexpr self_type operator~() const noexcept
	{
		self_type result;
		for (size_type index = 0; index < size; ++index)
			result[index] = ~limbs_[index];
		return result;
	}

	constexpr self_type& operator|=(const self_type& rhs) noexcept
	{
		for (size_type index = 0; index < size; ++index)
			limbs_[index] |= rhs[index];
		return *this;
	}

	constexpr self_type& operator&=(const self_type& rhs) noexcept
	{
		for (size_type index = 0; index < size; ++index)
			limbs_[index] &= rhs[index];
		return *this;
	}

	constexpr self_type& operator^=(const self_type& rhs) noexcept
	{
		for (size_type index = 0; index < size; ++index)
			limbs_[index] ^= rhs[index];
		return *this;
	}

	[[nodiscard]] friend constexpr self_type operator|(self_type lhs, const self_type& rhs) noexcept
	{
		return lhs |= rhs;
	}

	[[nodiscard]] friend constexpr self_type operator&(self_type lhs, const self_type& rhs) noexcept
	{
		return lhs &= rhs;
	}

	[[nodiscard]] friend constexpr self_type operator^(self_type lhs, const self_type& rhs) noexcept
	{
		return lhs ^= rhs;
	}

	constexpr self_type& operator+=(const self_type& rhs) noexcept
	{
		base_type carry = 0;
		for (size_type index = 0; index < size; ++index)
		{
			const base_type first = limbs_[index] + rhs[index];
			const base_type first_carry = first < limbs_[index];
			const base_type sum = first + carry;
			const base_type second_carry = sum < first;
			limbs_[index] = sum;
			carry = first_carry | second_carry;
		}
		return *this;
	}

	constexpr self_type& operator-=(const self_type& rhs) noexcept
	{
		base_type borrow = 0;
		for (size_type index = 0; index < size; ++index)
		{
			const base_type first = limbs_[index] - rhs[index];
			const base_type first_borrow = limbs_[index] < rhs[index];
			const base_type difference = first - borrow;
			const base_type second_borrow = first < borrow;
			limbs_[index] = difference;
			borrow = first_borrow | second_borrow;
		}
		return *this;
	}

	[[nodiscard]] friend constexpr self_type operator+(self_type lhs, const self_type& rhs) noexcept
	{
		return lhs += rhs;
	}

	[[nodiscard]] friend constexpr self_type operator-(self_type lhs, const self_type& rhs) noexcept
	{
		return lhs -= rhs;
	}

	constexpr self_type& operator++() noexcept
	{
		return *this += self_type(1);
	}

	constexpr self_type operator++(int) noexcept
	{
		const self_type previous = *this;
		++*this;
		return previous;
	}

	constexpr self_type& operator--() noexcept
	{
		return *this -= self_type(1);
	}

	constexpr self_type operator--(int) noexcept
	{
		const self_type previous = *this;
		--*this;
		return previous;
	}

	[[nodiscard]] constexpr self_type operator-() const noexcept
	{
		return ~*this + self_type(1);
	}

	constexpr self_type& operator<<=(size_type shift) noexcept
	{
		if (shift >= bits)
		{
			limbs_.fill(0);
			return *this;
		}
		if (shift == 0)
			return *this;

		const size_type limb_shift = shift / base_bits;
		const size_type bit_shift = shift % base_bits;
		std::array<base_type, size> shifted{};
		for (size_type source = 0; source < size; ++source)
		{
			const size_type destination = source + limb_shift;
			if (destination >= size)
				break;
			shifted[destination] |= limbs_[source] << bit_shift;
			if (bit_shift != 0 && destination + 1 < size)
				shifted[destination + 1] |= limbs_[source] >> (base_bits - bit_shift);
		}
		limbs_ = shifted;
		return *this;
	}

	constexpr self_type& operator>>=(size_type shift) noexcept
	{
		if (shift >= bits)
		{
			limbs_.fill(0);
			return *this;
		}
		if (shift == 0)
			return *this;

		const size_type limb_shift = shift / base_bits;
		const size_type bit_shift = shift % base_bits;
		std::array<base_type, size> shifted{};
		for (size_type source = limb_shift; source < size; ++source)
		{
			const size_type destination = source - limb_shift;
			shifted[destination] |= limbs_[source] >> bit_shift;
			if (bit_shift != 0 && source + 1 < size)
				shifted[destination] |= limbs_[source + 1] << (base_bits - bit_shift);
		}
		limbs_ = shifted;
		return *this;
	}

	[[nodiscard]] friend constexpr self_type operator<<(self_type value, size_type shift) noexcept
	{
		return value <<= shift;
	}

	[[nodiscard]] friend constexpr self_type operator>>(self_type value, size_type shift) noexcept
	{
		return value >>= shift;
	}

	[[nodiscard]] constexpr self_type operator*(const self_type& rhs) const noexcept
	{
		self_type result;
		for (size_type lhs_index = 0; lhs_index < size; ++lhs_index)
		{
			base_type carry = 0;
			for (size_type rhs_index = 0; lhs_index + rhs_index < size; ++rhs_index)
			{
				const auto product = long_uint_detail::multiply_wide(
					limbs_[lhs_index], rhs[rhs_index]);
				base_type& destination = result[lhs_index + rhs_index];
				const base_type with_destination = product.low + destination;
				const base_type destination_carry = with_destination < product.low;
				const base_type with_carry = with_destination + carry;
				const base_type addition_carry = with_carry < with_destination;
				destination = with_carry;
				carry = product.high + destination_carry + addition_carry;
			}
		}
		return result;
	}

	constexpr self_type& operator*=(const self_type& rhs) noexcept
	{
		return *this = *this * rhs;
	}

	[[nodiscard]] static constexpr std::pair<self_type, self_type> divmod(
		const self_type& dividend,
		const self_type& divisor)
	{
		if (!divisor)
			throw std::domain_error("long_uint division by zero");

		self_type quotient;
		self_type remainder;
		for (size_type bit = bits; bit-- > 0;)
		{
			const bool overflow = remainder.get_bit(bits - 1);
			remainder <<= 1;
			remainder.set_bit(0, dividend.get_bit(bit));
			if (overflow || remainder >= divisor)
			{
				remainder -= divisor;
				quotient.set_bit(bit);
			}
		}
		return {quotient, remainder};
	}

	[[nodiscard]] constexpr self_type operator/(const self_type& rhs) const
	{
		return divmod(*this, rhs).first;
	}

	[[nodiscard]] constexpr self_type operator%(const self_type& rhs) const
	{
		return divmod(*this, rhs).second;
	}

	constexpr self_type& operator/=(const self_type& rhs)
	{
		return *this = *this / rhs;
	}

	constexpr self_type& operator%=(const self_type& rhs)
	{
		return *this = *this % rhs;
	}

	[[nodiscard]] friend constexpr bool operator==(const self_type&, const self_type&) noexcept = default;

	[[nodiscard]] friend constexpr std::strong_ordering operator<=> (
		const self_type& lhs,
		const self_type& rhs) noexcept
	{
		for (size_type index = size; index-- > 0;)
		{
			if (lhs[index] < rhs[index])
				return std::strong_ordering::less;
			if (lhs[index] > rhs[index])
				return std::strong_ordering::greater;
		}
		return std::strong_ordering::equal;
	}

	[[nodiscard]] constexpr size_type leading_zeros() const noexcept
	{
		for (size_type index = size; index-- > 0;)
		{
			if (limbs_[index] != 0)
				return (size - 1 - index) * base_bits +
					static_cast<size_type>(std::countl_zero(limbs_[index]));
		}
		return bits;
	}

	[[nodiscard]] constexpr size_type trailing_zeros() const noexcept
	{
		for (size_type index = 0; index < size; ++index)
		{
			if (limbs_[index] != 0)
				return index * base_bits +
					static_cast<size_type>(std::countr_zero(limbs_[index]));
		}
		return bits;
	}

	[[nodiscard]] constexpr size_type bit_length() const noexcept
	{
		return bits - leading_zeros();
	}

	[[nodiscard]] constexpr size_type __leading_zeros() const noexcept
	{
		return leading_zeros();
	}

	[[nodiscard]] constexpr size_type __trailing_zeros() const noexcept
	{
		return trailing_zeros();
	}

	[[nodiscard]] std::string to_string() const
	{
		if (!*this)
			return "0";

		constexpr base_type decimal_radix = 10'000'000'000'000'000'000ULL;
		constexpr size_type chunk_digits = 19;
		std::vector<base_type> chunks;
		self_type remaining = *this;
		while (remaining)
		{
			base_type remainder = 0;
			remaining = divmod_small(remaining, decimal_radix, remainder);
			chunks.push_back(remainder);
		}

		std::string result;
		char top_buffer[chunk_digits];
		const auto [top_end, top_error] = std::to_chars(
			top_buffer, top_buffer + chunk_digits, chunks.back());
		if (top_error != std::errc{})
			throw std::runtime_error("long_uint decimal formatting failed");
		result.append(top_buffer, top_end);

		for (size_type index = chunks.size() - 1; index-- > 0;)
		{
			char chunk[chunk_digits];
			base_type value = chunks[index];
			for (size_type digit = chunk_digits; digit-- > 0;)
			{
				chunk[digit] = static_cast<char>('0' + value % 10);
				value /= 10;
			}
			result.append(chunk, chunk_digits);
		}
		return result;
	}

	[[nodiscard]] static std::string to_string(self_type value)
	{
		return value.to_string();
	}

	[[nodiscard]] std::string to_hex_string(
		bool leading_zeros = false,
		bool prefix = true) const
	{
		static constexpr char digits_table[] = "0123456789abcdef";
		std::string result;
		if (prefix)
			result = "0x";

		bool started = leading_zeros;
		for (size_type nibble = bits / 4; nibble-- > 0;)
		{
			const size_type limb_index = nibble / 16;
			const size_type limb_shift = (nibble % 16) * 4;
			const unsigned int value = static_cast<unsigned int>((limbs_[limb_index] >> limb_shift) & 0xFU);
			if (value != 0 || started)
			{
				result.push_back(digits_table[value]);
				started = true;
			}
		}
		if (!started)
			result.push_back('0');
		return result;
	}

	[[nodiscard]] static std::optional<self_type> from_decimal(std::string_view text)
	{
		if (text.empty())
			return std::nullopt;

		base_type maximum_last_digit = 0;
		const self_type maximum_before_digit = divmod_small(
			maximum_value(), 10, maximum_last_digit);
		self_type result;
		for (const char character : text)
		{
			if (character < '0' || character > '9')
				return std::nullopt;
			const base_type digit = static_cast<base_type>(character - '0');
			if (result > maximum_before_digit ||
				(result == maximum_before_digit && digit > maximum_last_digit))
				return std::nullopt;
			result *= self_type(10);
			result += self_type(digit);
		}
		return result;
	}

	[[nodiscard]] static constexpr self_type maximum_value() noexcept
	{
		return self_type(std::numeric_limits<base_type>::max(), fill_fields_tag{});
	}

private:
	[[nodiscard]] static constexpr self_type divmod_small(
		const self_type& dividend,
		base_type divisor,
		base_type& remainder)
	{
		if (divisor == 0)
			throw std::domain_error("long_uint division by zero");

		self_type quotient;
		base_type rolling_remainder = 0;
		for (size_type index = size; index-- > 0;)
		{
#if defined(__SIZEOF_INT128__) && !defined(__wasm__)
			const __uint128_t combined =
				(static_cast<__uint128_t>(rolling_remainder) << 64) | dividend[index];
			quotient[index] = static_cast<base_type>(combined / divisor);
			rolling_remainder = static_cast<base_type>(combined % divisor);
#elif defined(_MSC_VER) && defined(_M_X64)
			quotient[index] = _udiv128(rolling_remainder, dividend[index], divisor, &rolling_remainder);
#else
			self_type single_limb_divisor(divisor);
			const auto [fallback_quotient, fallback_remainder] = divmod(dividend, single_limb_divisor);
			remainder = static_cast<base_type>(fallback_remainder);
			return fallback_quotient;
#endif
		}
		remainder = rolling_remainder;
		return quotient;
	}

	std::array<base_type, size> limbs_{};

	template<std::uint64_t>
	friend class long_uint;
};

template<std::uint64_t Degree>
inline std::ostream& operator<<(std::ostream& output, const long_uint<Degree>& value)
{
	return output << value.to_string();
}

template<std::uint64_t Degree>
inline std::istream& operator>>(std::istream& input, long_uint<Degree>& value)
{
	std::string text;
	input >> text;
	if (!input)
		return input;
	const auto parsed = long_uint<Degree>::from_decimal(text);
	if (!parsed)
		input.setstate(std::ios::failbit);
	else
		value = *parsed;
	return input;
}

} // namespace dixelu

namespace std
{

template<std::uint64_t Degree>
class numeric_limits<dixelu::long_uint<Degree>> : public numeric_limits<std::uint64_t>
{
	using value_type = dixelu::long_uint<Degree>;

public:
	static constexpr bool is_specialized = true;
	static constexpr value_type min() noexcept { return {}; }
	static constexpr value_type max() noexcept { return value_type::maximum_value(); }
	static constexpr value_type lowest() noexcept { return {}; }
	static constexpr int digits = static_cast<int>(value_type::bits);
	static constexpr int digits10 = static_cast<int>(value_type::bits * 0.30102999566398119521L);
	static constexpr int max_digits10 = 0;
	static constexpr bool is_signed = false;
	static constexpr bool is_integer = true;
	static constexpr bool is_exact = true;
	static constexpr int radix = 2;
	static constexpr value_type epsilon() noexcept { return {}; }
	static constexpr value_type round_error() noexcept { return {}; }
	static constexpr int min_exponent = 0;
	static constexpr int min_exponent10 = 0;
	static constexpr int max_exponent = 0;
	static constexpr int max_exponent10 = 0;
	static constexpr bool has_infinity = false;
	static constexpr bool has_quiet_NaN = false;
	static constexpr bool has_signaling_NaN = false;
	static constexpr value_type infinity() noexcept { return {}; }
	static constexpr value_type quiet_NaN() noexcept { return {}; }
	static constexpr value_type signaling_NaN() noexcept { return {}; }
	static constexpr value_type denorm_min() noexcept { return {}; }
	static constexpr bool is_iec559 = false;
	static constexpr bool is_bounded = true;
	static constexpr bool is_modulo = true;
	static constexpr bool traps = false;
	static constexpr bool tinyness_before = false;
	static constexpr float_round_style round_style = round_toward_zero;
};

} // namespace std
