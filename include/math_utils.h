#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace dixelu::utils
{

template<typename T>
[[nodiscard]] constexpr T constexpr_abs(T value)
{
	if constexpr (std::numeric_limits<T>::is_integer && std::numeric_limits<T>::is_signed)
	{
		if (value == std::numeric_limits<T>::lowest())
			throw std::overflow_error("absolute value is not representable");
	}
	return value < T(0) ? -value : value;
}

template<typename T>
[[nodiscard]] constexpr const T& constexpr_min(const T& lhs, const T& rhs) noexcept(noexcept(rhs < lhs))
{
	return rhs < lhs ? rhs : lhs;
}

template<typename T>
[[nodiscard]] constexpr const T& constexpr_max(const T& lhs, const T& rhs) noexcept(noexcept(lhs < rhs))
{
	return lhs < rhs ? rhs : lhs;
}

namespace details
{

template<typename T, bool = std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>>
struct try_unsigned_impl
{
	using type = T;
};

template<typename T>
struct try_unsigned_impl<T, true>
{
	using type = std::make_unsigned_t<T>;
};

template<typename T>
struct try_unsigned : try_unsigned_impl<T> {};

template<typename T, bool = std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>>
struct try_signed_impl
{
	using type = T;
};

template<typename T>
struct try_signed_impl<T, true>
{
	using type = std::make_signed_t<T>;
};

template<typename T>
struct try_signed : try_signed_impl<T> {};

template<typename T>
struct opposite_sign_type
{
	using type = std::conditional_t<
		std::is_signed_v<T>,
		typename try_unsigned<T>::type,
		typename try_signed<T>::type>;
};

template<typename T, std::unsigned_integral Exponent>
[[nodiscard]] constexpr T unsigned_power(T base, Exponent exponent)
{
	T result(1);
	while (exponent != 0)
	{
		if ((exponent & 1U) != 0)
			result *= base;
		exponent >>= 1U;
		if (exponent != 0)
			base *= base;
	}
	return result;
}

template<typename T>
[[nodiscard]] constexpr bool power_fits(T base, std::size_t exponent, const T& limit)
{
	T product(1);
	for (std::size_t count = 0; count < exponent; ++count)
	{
		if (product > limit / base)
			return false;
		product *= base;
	}
	return true;
}

template<std::floating_point T>
[[nodiscard]] constexpr T constexpr_log_positive(T value)
{
	constexpr T ln2 = static_cast<T>(0.693147180559945309417232121458176568L);
	int binary_scale = 0;
	while (value > static_cast<T>(1.5))
	{
		value /= static_cast<T>(2);
		++binary_scale;
	}
	while (value < static_cast<T>(0.75))
	{
		value *= static_cast<T>(2);
		--binary_scale;
	}

	const T ratio = (value - T(1)) / (value + T(1));
	const T ratio_squared = ratio * ratio;
	T term = ratio;
	T sum = term;
	for (std::size_t denominator = 3; denominator < 1024; denominator += 2)
	{
		term *= ratio_squared;
		const T next = sum + term / static_cast<T>(denominator);
		if (next == sum)
			break;
		sum = next;
	}
	return static_cast<T>(2) * sum + static_cast<T>(binary_scale) * ln2;
}

template<std::floating_point T>
[[nodiscard]] constexpr T constexpr_exp(T value)
{
	constexpr T ln2 = static_cast<T>(0.693147180559945309417232121458176568L);
	int binary_scale = 0;
	while (value > static_cast<T>(0.5))
	{
		value -= ln2;
		++binary_scale;
	}
	while (value < static_cast<T>(-0.5))
	{
		value += ln2;
		--binary_scale;
	}

	T sum(1);
	T term(1);
	for (std::size_t index = 1; index < 1024; ++index)
	{
		term *= value / static_cast<T>(index);
		const T next = sum + term;
		if (next == sum)
			break;
		sum = next;
	}
	while (binary_scale > 0)
	{
		sum *= static_cast<T>(2);
		--binary_scale;
	}
	while (binary_scale < 0)
	{
		sum /= static_cast<T>(2);
		++binary_scale;
	}
	return sum;
}

} // namespace details

template<typename T>
[[nodiscard]] constexpr T constexpr_intpow(T base, std::ptrdiff_t exponent)
{
	using unsigned_exponent = std::make_unsigned_t<std::ptrdiff_t>;
	const bool inverse = exponent < 0;
	const unsigned_exponent magnitude = inverse
		? unsigned_exponent{0} - static_cast<unsigned_exponent>(exponent)
		: static_cast<unsigned_exponent>(exponent);
	const T powered = details::unsigned_power(base, magnitude);
	if (inverse && powered == T(0))
		throw std::domain_error("zero cannot be raised to a negative power");
	return inverse ? T(1) / powered : powered;
}

template<std::size_t Root, typename T>
	requires (Root > 0 && std::numeric_limits<T>::is_integer)
[[nodiscard]] constexpr T integer_nth_root(T value)
{
	if constexpr (Root == 1)
		return value;

	if constexpr (std::numeric_limits<T>::is_signed)
	{
		if (value < 0)
		{
			if constexpr ((Root & 1U) == 0)
				throw std::domain_error("even root of a negative integer");
			using unsigned_type = std::make_unsigned_t<T>;
			const unsigned_type magnitude = unsigned_type{0} - static_cast<unsigned_type>(value);
			return -static_cast<T>(integer_nth_root<Root>(magnitude));
		}
	}

	if (value <= T(1))
		return value;

	constexpr std::size_t value_bits = static_cast<std::size_t>(std::numeric_limits<T>::digits);
	constexpr std::size_t root_bits = (value_bits + Root - 1) / Root;
	T result(0);
	for (std::size_t bit = root_bits; bit-- > 0;)
	{
		const T candidate = result | (T(1) << bit);
		if (details::power_fits(candidate, Root, value))
			result = candidate;
	}
	return result;
}

template<typename T>
[[nodiscard]] constexpr T constexpr_sqrt(T value)
{
	if constexpr (std::numeric_limits<T>::is_integer)
	{
		return integer_nth_root<2>(value);
	}
	else
	{
		if (!std::is_constant_evaluated())
			return std::sqrt(value);
		if (value < T(0))
			return std::numeric_limits<T>::quiet_NaN();
		if (value == T(0))
			return T(0);

		T estimate = value < T(1) ? T(1) : value;
		for (std::size_t iteration = 0; iteration < 1024; ++iteration)
		{
			const T next = (estimate + value / estimate) / T(2);
			if (next == estimate)
				break;
			estimate = next;
		}
		return estimate;
	}
}

template<typename T>
	requires std::numeric_limits<T>::is_integer
[[nodiscard]] constexpr T constexpr_pow(T base, std::ptrdiff_t exponent)
{
	return constexpr_intpow(base, exponent);
}

template<std::floating_point T>
[[nodiscard]] constexpr T constexpr_pow(T base, T exponent)
{
	if (!std::is_constant_evaluated())
		return std::pow(base, exponent);

	if (exponent >= static_cast<T>(std::numeric_limits<std::ptrdiff_t>::min()) &&
		exponent <= static_cast<T>(std::numeric_limits<std::ptrdiff_t>::max()))
	{
		const auto integer_exponent = static_cast<std::ptrdiff_t>(exponent);
		if (static_cast<T>(integer_exponent) == exponent)
			return constexpr_intpow(base, integer_exponent);
	}

	if (base <= T(0))
		throw std::domain_error("fractional power requires a positive base");
	return details::constexpr_exp(exponent * details::constexpr_log_positive(base));
}

} // namespace dixelu::utils
