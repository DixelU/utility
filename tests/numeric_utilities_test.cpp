#include <boost/multiprecision/cpp_int.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
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

// Limbs are zero, boundary values or random, so operands have the empty
// leading and interior limbs that full-width random values never produce.
template<std::uint64_t Degree>
dixelu::long_uint<Degree> sparse_value(std::mt19937_64& random)
{
	constexpr std::array<std::uint64_t, 6> special{
		0, 0, 1, 0x8000000000000000ULL, ~0ULL, 0x00000000FFFFFFFFULL};
	dixelu::long_uint<Degree> result;
	for (std::size_t index = 0; index < result.size; ++index)
	{
		const std::uint64_t pick = random();
		result[index] = pick % 3 == 0 ? random() : special[(pick >> 8) % special.size()];
	}
	return result;
}

template<std::uint64_t Degree>
void test_long_uint_sparse_degree(std::mt19937_64& random, std::size_t iterations)
{
	using value_type = dixelu::long_uint<Degree>;
	const cpp_int mask = (cpp_int(1) << value_type::bits) - 1;

	for (std::size_t iteration = 0; iteration < iterations; ++iteration)
	{
		const value_type lhs = sparse_value<Degree>(random);
		value_type rhs = sparse_value<Degree>(random);
		if (iteration % 2 == 0)
		{
			// Single-limb divisor: the fast path in divmod.
			rhs = value_type(sparse_value<Degree>(random)[0]);
		}
		if (!rhs)
			rhs = value_type(1);
		const cpp_int lhs_reference = to_reference(lhs);
		const cpp_int rhs_reference = to_reference(rhs);

		const std::size_t expected_used = lhs_reference == 0
			? 0
			: boost::multiprecision::msb(lhs_reference) / 64 + 1;
		require(lhs.used_limbs() == expected_used, "long_uint used limb count mismatch");
		require(to_reference(lhs * rhs) == ((lhs_reference * rhs_reference) & mask), "long_uint sparse multiplication mismatch");
		require(to_reference(rhs * lhs) == ((lhs_reference * rhs_reference) & mask), "long_uint sparse multiplication is not commutative");

		const auto [quotient, remainder] = value_type::divmod(lhs, rhs);
		require(to_reference(quotient) == lhs_reference / rhs_reference, "long_uint sparse division mismatch");
		require(to_reference(remainder) == lhs_reference % rhs_reference, "long_uint sparse remainder mismatch");
		require(lhs.to_string() == lhs_reference.convert_to<std::string>(), "long_uint sparse decimal formatting mismatch");
	}
}

// Divides (high:low) by divisor; high < divisor and divisor != 0 are the preconditions.
void test_long_uint_divide_wide(std::mt19937_64& random, std::size_t iterations)
{
	using namespace dixelu::long_uint_detail;

	const auto check = [](std::uint64_t high, std::uint64_t low, std::uint64_t divisor)
	{
		const cpp_int dividend = (cpp_int(high) << 64) | low;
		const cpp_int expected_quotient = dividend / divisor;
		const cpp_int expected_remainder = dividend % divisor;

		std::uint64_t portable_remainder = 0;
		const std::uint64_t portable_quotient = divide_wide_impl(high, low, divisor, portable_remainder);
		require(
			portable_quotient == expected_quotient && portable_remainder == expected_remainder,
			"portable 128-by-64 division mismatch");

		std::uint64_t native_remainder = 0;
		const std::uint64_t native_quotient = divide_wide(high, low, divisor, native_remainder);
		require(
			native_quotient == expected_quotient && native_remainder == expected_remainder,
			"native 128-by-64 division mismatch");
	};

	const auto make_divisor = [&random]() -> std::uint64_t
	{
		std::uint64_t divisor = 0;
		switch (random() % 6)
		{
		case 0: divisor = random(); break;
		case 1: divisor = random() >> (random() % 63); break;
		// A leading digit of 0x80000000 is where the quotient estimate overshoots most.
		case 2: divisor = (((0x80000000ULL + random() % 8) << 32) | (random() % 2 ? 0xFFFFFFFFULL : random() % 4)) >> (random() % 32); break;
		case 3: divisor = (std::uint64_t{1} << 63) | (random() % 4); break;
		case 4: divisor = ~std::uint64_t{0} - random() % 3; break;
		default: divisor = (std::uint64_t{1} << (random() % 64)) + random() % 3; break;
		}
		return divisor == 0 ? 1 : divisor;
	};

	for (std::size_t iteration = 0; iteration < iterations; ++iteration)
	{
		const std::uint64_t divisor = make_divisor();
		std::uint64_t high = 0;
		switch (random() % 4)
		{
		case 0: high = 0; break;
		case 1: high = divisor - 1; break;
		case 2: high = random() % divisor; break;
		default: high = divisor - 1 - random() % (divisor < 3 ? divisor : 3); break;
		}
		std::uint64_t low = 0;
		switch (random() % 4)
		{
		case 0: low = 0; break;
		case 1: low = 1; break;
		case 2: low = ~std::uint64_t{0}; break;
		default: low = random(); break;
		}
		check(high, low, divisor);
	}
}

template<std::uint64_t Degree>
dixelu::long_uint<Degree> from_reference(const cpp_int& value)
{
	const cpp_int limb_mask(~std::uint64_t{0});
	dixelu::long_uint<Degree> result;
	for (std::size_t index = 0; index < result.size; ++index)
	{
		const cpp_int limb = (value >> (64 * index)) & limb_mask;
		result[index] = limb.convert_to<std::uint64_t>();
	}
	return result;
}

template<std::uint64_t Degree>
dixelu::long_uint<Degree> from_limbs(std::initializer_list<std::uint64_t> limbs)
{
	dixelu::long_uint<Degree> result;
	require(limbs.size() <= result.size, "from_limbs given more limbs than the type holds");
	std::size_t index = 0;
	for (const std::uint64_t limb : limbs)
		result[index++] = limb;
	return result;
}

template<std::uint64_t Degree>
void check_division(const dixelu::long_uint<Degree>& dividend, const dixelu::long_uint<Degree>& divisor)
{
	const auto [quotient, remainder] = dixelu::long_uint<Degree>::divmod(dividend, divisor);
	const cpp_int dividend_reference = to_reference(dividend);
	const cpp_int divisor_reference = to_reference(divisor);
	require(to_reference(quotient) == dividend_reference / divisor_reference, "long_uint multi-limb quotient mismatch");
	require(to_reference(remainder) == dividend_reference % divisor_reference, "long_uint multi-limb remainder mismatch");
}

// Divisors of two or more limbs, with dividends built around them (exact
// multiples, maximal remainders) so the digit estimate is wrong as often as
// possible, including the rare add-back step.
template<std::uint64_t Degree>
void test_long_uint_multi_limb_division(std::mt19937_64& random, std::size_t iterations)
{
	using value_type = dixelu::long_uint<Degree>;
	constexpr std::uint64_t all = ~std::uint64_t{0};
	constexpr std::uint64_t high_bit = std::uint64_t{1} << 63;

	// Limbs are listed least significant first. This is the classic add-back
	// case: the estimate saturates, is still one too high after the
	// second-limb check, and the multiply-subtract goes negative.
	if constexpr (value_type::size >= 3)
	{
		check_division<Degree>(from_limbs<Degree>({0, all - 1, high_bit}), from_limbs<Degree>({all, high_bit}));
		check_division<Degree>(from_limbs<Degree>({all, all, all}), from_limbs<Degree>({all, all}));
		check_division<Degree>(from_limbs<Degree>({0, 0, high_bit}), from_limbs<Degree>({1, high_bit}));
		check_division<Degree>(from_limbs<Degree>({7, 0, 1}), from_limbs<Degree>({1, 1}));
	}
	check_division<Degree>(from_limbs<Degree>({0, 1}), from_limbs<Degree>({0, 1}));
	check_division<Degree>(from_limbs<Degree>({5, 1}), from_limbs<Degree>({6, 1}));
	check_division<Degree>(from_limbs<Degree>({1, 1}), from_limbs<Degree>({1, 1}));
	check_division<Degree>(value_type::maximum_value(), from_limbs<Degree>({all, all}));
	check_division<Degree>(value_type::maximum_value(), from_limbs<Degree>({0, 1}));
	check_division<Degree>(value_type::maximum_value(), from_limbs<Degree>({1, high_bit}));
	check_division<Degree>(from_limbs<Degree>({0, high_bit}), from_limbs<Degree>({all, all}));

	const cpp_int mask = (cpp_int(1) << value_type::bits) - 1;
	constexpr std::array<std::uint64_t, 8> edge{
		0, 1, 2, 0x7FFFFFFFFFFFFFFFULL, 0x8000000000000000ULL, 0x8000000000000001ULL, ~0ULL - 1, ~0ULL};
	const auto pick = [&random, &edge]() -> std::uint64_t
	{
		return random() % 2 == 0 ? edge[random() % edge.size()] : random();
	};

	for (std::size_t iteration = 0; iteration < iterations; ++iteration)
	{
		const std::size_t divisor_limbs = 2 + random() % (value_type::size - 1);
		value_type divisor;
		for (std::size_t index = 0; index < divisor_limbs; ++index)
			divisor[index] = pick();
		if (divisor[divisor_limbs - 1] == 0)
			divisor[divisor_limbs - 1] = edge[1 + random() % (edge.size() - 1)];
		const cpp_int divisor_reference = to_reference(divisor);

		cpp_int dividend_reference = 0;
		switch (random() % 4)
		{
		case 0:
			dividend_reference = to_reference(sparse_value<Degree>(random));
			break;
		case 1:
		{
			// divisor * quotient + remainder, with remainder zero, maximal or random
			value_type quotient;
			const std::size_t quotient_limbs = 1 + random() % (value_type::size - divisor_limbs + 1);
			for (std::size_t index = 0; index < quotient_limbs; ++index)
				quotient[index] = pick();
			cpp_int remainder = 0;
			switch (random() % 3)
			{
			case 0: break;
			case 1: remainder = divisor_reference - 1; break;
			default: remainder = to_reference(random_value<Degree>(random)) % divisor_reference; break;
			}
			dividend_reference = divisor_reference * to_reference(quotient) + remainder;
			break;
		}
		case 2:
			// a limb-aligned multiple of the divisor, minus one
			dividend_reference = (divisor_reference << (64 * (random() % (value_type::size - divisor_limbs + 1)))) - 1;
			break;
		default:
			dividend_reference = to_reference(random_value<Degree>(random)) >> (random() % value_type::bits);
			break;
		}
		dividend_reference &= mask;
		check_division<Degree>(from_reference<Degree>(dividend_reference), divisor);
	}
}

template<std::uint64_t Degree>
constexpr bool constexpr_divmod_consistent(
	const dixelu::long_uint<Degree>& dividend,
	const dixelu::long_uint<Degree>& divisor)
{
	const auto [quotient, remainder] = dixelu::long_uint<Degree>::divmod(dividend, divisor);
	return remainder < divisor && quotient * divisor + remainder == dividend;
}

void test_long_uint()
{
	static_assert(dixelu::long_uint<0>::bits == 128);
	static_assert(dixelu::long_uint<1>::bits == 256);
	static_assert(std::numeric_limits<dixelu::long_uint<0>>::is_modulo);

	// Constant evaluation takes the portable division, whatever the runtime path is.
	constexpr auto make_wide = [](std::uint64_t low, std::uint64_t high)
	{
		dixelu::long_uint<0> value;
		value[0] = low;
		value[1] = high;
		return value;
	};
	static_assert(dixelu::long_uint<0>::divmod(make_wide(1000, 0), make_wide(7, 0)).first == make_wide(142, 0));
	static_assert(dixelu::long_uint<0>::divmod(make_wide(1000, 0), make_wide(7, 0)).second == make_wide(6, 0));
	static_assert(constexpr_divmod_consistent(make_wide(0x0123456789ABCDEFULL, 0x00000000DEADBEEFULL), make_wide(1'000'000'007, 0)));
	static_assert(constexpr_divmod_consistent(make_wide(0, 0x8000000000000000ULL), make_wide(~0ULL, 0)));
	static_assert(constexpr_divmod_consistent(make_wide(~0ULL, ~0ULL), make_wide(1, 0)));
	static_assert(constexpr_divmod_consistent(make_wide(~0ULL, ~0ULL), make_wide(~0ULL, 0)));
	static_assert(constexpr_divmod_consistent(make_wide(12345, 0xABCDEF), make_wide(99, 0x1234)));
	static_assert(constexpr_divmod_consistent(make_wide(7, 0), make_wide(0, 1)));
	static_assert(make_wide(0, 0).used_limbs() == 0 && make_wide(5, 0).used_limbs() == 1 && make_wide(0, 5).used_limbs() == 2);

	// Three-limb dividend over two-limb divisor: saturated estimate plus the add-back step.
	constexpr auto make_wider = [](std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d)
	{
		dixelu::long_uint<1> value;
		value[0] = a;
		value[1] = b;
		value[2] = c;
		value[3] = d;
		return value;
	};
	static_assert(constexpr_divmod_consistent(
		make_wider(0, ~0ULL - 1, 0x8000000000000000ULL, 0), make_wider(~0ULL, 0x8000000000000000ULL, 0, 0)));
	static_assert(constexpr_divmod_consistent(
		make_wider(~0ULL, ~0ULL, ~0ULL, ~0ULL), make_wider(~0ULL, ~0ULL, 1, 0)));
	static_assert(constexpr_divmod_consistent(
		make_wider(123, 456, 789, 1011), make_wider(1, 2, 3, 4)));

	std::mt19937_64 random(0xA11CE5EEDULL);
	test_long_uint_degree<0>(random, 1500);
	test_long_uint_degree<1>(random, 500);
	test_long_uint_sparse_degree<0>(random, 20000);
	test_long_uint_sparse_degree<1>(random, 20000);
	test_long_uint_divide_wide(random, 400000);
	test_long_uint_multi_limb_division<0>(random, 100000);
	test_long_uint_multi_limb_division<1>(random, 100000);

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
