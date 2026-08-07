#include <boost/multiprecision/cpp_int.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "long_uint.h"
#include "math_utils.h"

namespace
{

using boost::multiprecision::cpp_int;

void require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

template<typename Function>
bool throws_domain_error(Function&& function)
{
	try
	{
		std::forward<Function>(function)();
		return false;
	}
	catch (const std::domain_error&)
	{
		return true;
	}
}

template<typename Function>
bool throws_overflow_error(Function&& function)
{
	try
	{
		std::forward<Function>(function)();
		return false;
	}
	catch (const std::overflow_error&)
	{
		return true;
	}
}

template<std::uint64_t Degree>
cpp_int to_reference(const dixelu::long_uint<Degree>& value)
{
	cpp_int result = 0;
	for (std::size_t index = dixelu::long_uint<Degree>::size; index-- > 0;)
	{
		result <<= 64;
		result += value[index];
	}
	return result;
}

template<std::uint64_t Degree>
dixelu::long_uint<Degree> random_value(std::mt19937_64& random)
{
	dixelu::long_uint<Degree> result;
	for (std::size_t index = 0; index < result.size; ++index)
		result[index] = random();
	return result;
}

std::string reference_hex(const cpp_int& value)
{
	std::ostringstream output;
	output << "0x" << std::hex << value;
	return output.str();
}

cpp_int reference_sqrt(const cpp_int& value)
{
	if (value == 0)
		return 0;
	cpp_int estimate = cpp_int(1) << ((boost::multiprecision::msb(value) + 2) / 2);
	while (true)
	{
		const cpp_int next = (estimate + value / estimate) / 2;
		if (next >= estimate)
			return estimate;
		estimate = next;
	}
}

template<std::uint64_t Degree>
void test_long_uint_degree(std::mt19937_64& random, std::size_t iterations)
{
	using value_type = dixelu::long_uint<Degree>;
	const cpp_int modulus = cpp_int(1) << value_type::bits;
	const cpp_int mask = modulus - 1;
	const std::array<std::size_t, 11> shifts{
		0, 1, 31, 63, 64, 65, value_type::bits / 2,
		value_type::bits - 1, value_type::bits, value_type::bits + 1, value_type::bits * 2};

	for (std::size_t iteration = 0; iteration < iterations; ++iteration)
	{
		const value_type lhs = random_value<Degree>(random);
		value_type rhs = random_value<Degree>(random);
		if (!rhs)
			rhs = value_type(1);
		const cpp_int lhs_reference = to_reference(lhs);
		const cpp_int rhs_reference = to_reference(rhs);

		require(to_reference(lhs + rhs) == ((lhs_reference + rhs_reference) & mask), "long_uint addition mismatch");
		require(to_reference(lhs - rhs) == ((lhs_reference - rhs_reference) & mask), "long_uint subtraction mismatch");
		require(to_reference(lhs * rhs) == ((lhs_reference * rhs_reference) & mask), "long_uint multiplication mismatch");
		require(to_reference(lhs / rhs) == lhs_reference / rhs_reference, "long_uint division mismatch");
		require(to_reference(lhs % rhs) == lhs_reference % rhs_reference, "long_uint remainder mismatch");
		require(to_reference(lhs & rhs) == (lhs_reference & rhs_reference), "long_uint bitwise-and mismatch");
		require(to_reference(lhs | rhs) == (lhs_reference | rhs_reference), "long_uint bitwise-or mismatch");
		require(to_reference(lhs ^ rhs) == (lhs_reference ^ rhs_reference), "long_uint bitwise-xor mismatch");
		require(
			(lhs < rhs) == (lhs_reference < rhs_reference) &&
			(lhs == rhs) == (lhs_reference == rhs_reference) &&
			(lhs > rhs) == (lhs_reference > rhs_reference),
			"long_uint comparison mismatch");

		for (const std::size_t shift : shifts)
		{
			const cpp_int expected_left = shift >= value_type::bits
				? cpp_int(0)
				: (lhs_reference << shift) & mask;
			const cpp_int expected_right = shift >= value_type::bits
				? cpp_int(0)
				: lhs_reference >> shift;
			require(to_reference(lhs << shift) == expected_left, "long_uint left shift mismatch");
			require(to_reference(lhs >> shift) == expected_right, "long_uint right shift mismatch");
		}

		const std::size_t expected_leading = lhs_reference == 0
			? value_type::bits
			: value_type::bits - 1 - boost::multiprecision::msb(lhs_reference);
		const std::size_t expected_trailing = lhs_reference == 0
			? value_type::bits
			: boost::multiprecision::lsb(lhs_reference);
		require(lhs.leading_zeros() == expected_leading, "long_uint leading-zero count mismatch");
		require(lhs.trailing_zeros() == expected_trailing, "long_uint trailing-zero count mismatch");

		const std::string decimal = lhs.to_string();
		require(decimal == lhs_reference.convert_to<std::string>(), "long_uint decimal formatting mismatch");
		const auto parsed = value_type::from_decimal(decimal);
		require(parsed && *parsed == lhs, "long_uint decimal round trip mismatch");
		require(lhs.to_hex_string() == reference_hex(lhs_reference), "long_uint hexadecimal formatting mismatch");
	}

	require(throws_domain_error([] { (void)(value_type(1) / value_type(0)); }), "long_uint division by zero must throw");
	require(!value_type::from_decimal(""), "long_uint must reject empty decimal input");
	require(!value_type::from_decimal("12x"), "long_uint must reject non-decimal input");
	require(
		!value_type::from_decimal((mask + 1).convert_to<std::string>()),
		"long_uint must reject decimal overflow");
	const value_type maximum = value_type::maximum_value();
	require(maximum / value_type(1) == maximum, "long_uint maximum divided by one mismatch");
	require(maximum % value_type(1) == value_type(0), "long_uint maximum remainder one mismatch");
	require(maximum / maximum == value_type(1), "long_uint self-division mismatch");
	require(maximum * maximum == value_type(1), "long_uint maximum square must wrap to one");
	require(
		value_type::from_decimal(mask.convert_to<std::string>()) == maximum,
		"long_uint must parse its maximum value");

	value_type preserved(7);
	std::istringstream invalid_input("not-a-number");
	invalid_input >> preserved;
	require(invalid_input.fail() && preserved == value_type(7), "invalid stream input must set failbit and preserve value");
}

void test_long_uint()
{
	static_assert(dixelu::long_uint<0>::bits == 128);
	static_assert(dixelu::long_uint<1>::bits == 256);
	static_assert(std::numeric_limits<dixelu::long_uint<0>>::is_modulo);

	std::mt19937_64 random(0xA11CE5EEDULL);
	test_long_uint_degree<0>(random, 1500);
	test_long_uint_degree<1>(random, 500);

	dixelu::long_uint<0> low;
	low[0] = 0x0123456789ABCDEFULL;
	low[1] = 0xFEDCBA9876543210ULL;
	const dixelu::long_uint<1> widened(low);
	require(to_reference(widened) == to_reference(low), "long_uint widening conversion must zero-extend");
	require(dixelu::long_uint<0>(widened) == low, "long_uint narrowing conversion must retain low limbs");
}

void test_math_utils()
{
	using namespace dixelu::utils;
	static_assert(constexpr_abs(-7) == 7);
	static_assert(constexpr_intpow(-2, 2) == 4);
	static_assert(constexpr_intpow(-2, 3) == -8);
	static_assert(integer_nth_root<2>(144U) == 12U);
	static_assert(integer_nth_root<2>(15U) == 3U);
	static_assert(integer_nth_root<3>(28U) == 3U);
	constexpr double compile_time_sqrt = constexpr_sqrt(2.0);
	constexpr double compile_time_power = constexpr_pow(9.0, 0.5);
	static_assert(compile_time_sqrt > 1.414 && compile_time_sqrt < 1.415);
	static_assert(compile_time_power > 2.999 && compile_time_power < 3.001);
	static_assert(std::is_same_v<details::try_signed<unsigned int>::type, int>);
	static_assert(std::is_same_v<details::try_unsigned<int>::type, unsigned int>);

	require(
		throws_overflow_error([] { (void)constexpr_abs(std::numeric_limits<int>::lowest()); }),
		"constexpr_abs must reject an unrepresentable signed magnitude");
	require(
		throws_domain_error([] { (void)integer_nth_root<2>(-1); }),
		"even integer root must reject negative input");
	require(integer_nth_root<3>(-28) == -3, "odd integer root must floor magnitude and preserve sign");
	require(constexpr_intpow(2.0, -3) == 0.125, "negative integer exponent must produce a reciprocal");
	require(
		throws_domain_error([] { (void)constexpr_intpow(0, -1); }),
		"negative powers of integer zero must throw");

	std::mt19937_64 random(0x5A17B00BULL);
	for (std::size_t iteration = 0; iteration < 20'000; ++iteration)
	{
		const std::uint64_t value = random();
		std::uint64_t square_root = static_cast<std::uint64_t>(std::sqrt(static_cast<long double>(value)));
		while (square_root + 1 != 0 && square_root + 1 <= value / (square_root + 1))
			++square_root;
		while (square_root != 0 && square_root > value / square_root)
			--square_root;
		require(integer_nth_root<2>(value) == square_root, "uint64 square root mismatch");

		const std::uint64_t cube_root = integer_nth_root<3>(value);
		const cpp_int cube = cpp_int(cube_root) * cube_root * cube_root;
		const cpp_int next_cube = cpp_int(cube_root + 1) * (cube_root + 1) * (cube_root + 1);
		require(cube <= value && next_cube > value, "uint64 cube root mismatch");
	}

	for (std::size_t iteration = 0; iteration < 100; ++iteration)
	{
		const auto value = random_value<0>(random);
		const auto actual = integer_nth_root<2>(value);
		require(to_reference(actual) == reference_sqrt(to_reference(value)), "long_uint square root mismatch");
	}

	for (std::size_t iteration = 0; iteration < 1000; ++iteration)
	{
		const double base = 0.125 + static_cast<double>(random() % 100'000) / 1000.0;
		const double exponent = -4.0 + static_cast<double>(random() % 8000) / 1000.0;
		require(constexpr_sqrt(base) == std::sqrt(base), "runtime floating square root must use std::sqrt");
		require(constexpr_pow(base, exponent) == std::pow(base, exponent), "runtime floating power must use std::pow");
	}
}

} // namespace

int main()
{
	test_long_uint();
	test_math_utils();
}
