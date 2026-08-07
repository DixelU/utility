#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

#include "polyline_converter.h"

namespace
{

void require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

template<typename Exception, typename Function>
bool throws(Function&& function)
{
	try
	{
		std::forward<Function>(function)();
		return false;
	}
	catch (const Exception&)
	{
		return true;
	}
}

bool approximately_equal(long double left, long double right, long double tolerance = 1e-12L)
{
	const long double scale = (std::max)({1.0L, std::abs(left), std::abs(right)});
	return std::abs(left - right) <= tolerance * scale;
}

using reference_point = std::pair<long double, long double>;

std::optional<long double> reference_evaluate(
	const std::vector<reference_point>& points,
	long double key,
	dixelu::polyline_extrapolation extrapolation)
{
	if (points.empty() || !std::isfinite(key))
		return std::nullopt;
	for (const auto& [point_key, point_value] : points)
		if (point_key == key)
			return point_value;

	if (points.size() == 1)
		return extrapolation == dixelu::polyline_extrapolation::reject
			? std::nullopt
			: std::optional<long double>(points.front().second);

	std::size_t right = 0;
	while (right < points.size() && points[right].first < key)
		++right;
	if (right == 0)
	{
		if (extrapolation == dixelu::polyline_extrapolation::reject)
			return std::nullopt;
		if (extrapolation == dixelu::polyline_extrapolation::clamp)
			return points.front().second;
		right = 1;
	}
	else if (right == points.size())
	{
		if (extrapolation == dixelu::polyline_extrapolation::reject)
			return std::nullopt;
		if (extrapolation == dixelu::polyline_extrapolation::clamp)
			return points.back().second;
		right = points.size() - 1;
	}

	const reference_point& left_point = points[right - 1];
	const reference_point& right_point = points[right];
	const long double ratio = (key - left_point.first) / (right_point.first - left_point.first);
	return left_point.second + ratio * (right_point.second - left_point.second);
}

void require_optional_equal(
	const std::optional<long double>& actual,
	const std::optional<long double>& expected,
	const char* message)
{
	require(actual.has_value() == expected.has_value(), message);
	if (actual)
		require(approximately_equal(*actual, *expected), message);
}

void test_curve_contracts()
{
	using curve = dixelu::polyline_converter<int, int>;
	using dixelu::polyline_extrapolation;
	using dixelu::polyline_integral_rounding;
	using dixelu::polyline_point_update;

	const curve empty;
	require(!empty.evaluate(0), "empty polyline must not synthesize an identity value");
	require(!empty.evaluate(0, polyline_extrapolation::linear), "empty polyline cannot extrapolate");
	require(throws<std::out_of_range>([&] { (void)empty.at(0); }), "polyline at must throw when no result exists");

	const curve single{{{10, 42}}};
	require(single.at(10) == 42.0L, "single polyline exact query mismatch");
	require(!single.evaluate(9), "single polyline reject policy mismatch");
	require(single.at(9, polyline_extrapolation::clamp) == 42.0L, "single polyline clamp mismatch");
	require(single.at(11, polyline_extrapolation::linear) == 42.0L, "single polyline linear fallback must be constant");

	curve value{{{0, 0}, {10, 100}, {20, 50}}};
	require(value.at(0) == 0.0L && value.at(10) == 100.0L, "polyline exact point mismatch");
	require(value.at(5) == 50.0L && value.at(15) == 75.0L, "polyline interpolation mismatch");
	require(!value.evaluate(-1) && !value.evaluate(21), "polyline reject extrapolation mismatch");
	require(value.at(-5, polyline_extrapolation::clamp) == 0.0L, "polyline lower clamp mismatch");
	require(value.at(30, polyline_extrapolation::clamp) == 50.0L, "polyline upper clamp mismatch");
	require(value.at(-5, polyline_extrapolation::linear) == -50.0L, "polyline lower linear extrapolation mismatch");
	require(value.at(30, polyline_extrapolation::linear) == 0.0L, "polyline upper linear extrapolation mismatch");

	require(value.insert_or_assign(30, 80) == polyline_point_update::inserted, "polyline insertion status mismatch");
	require(value.insert_or_assign(30, 80) == polyline_point_update::unchanged, "polyline unchanged status mismatch");
	require(value.insert_or_assign(30, 90) == polyline_point_update::replaced, "polyline replacement status mismatch");
	require(value.erase(30) && !value.erase(30), "polyline erase status mismatch");
	const curve duplicate{{{0, 1}, {0, 7}}};
	require(duplicate.size() == 1 && duplicate.at(0) == 7.0L, "duplicate polyline keys must use the last value");

	const dixelu::polyline_converter<std::uint16_t, std::uint16_t> descending{{{0, 60'000}, {1'000, 0}}};
	require(descending.at(500) == 30'000.0L, "unsigned interpolation must not underflow intermediate arithmetic");

	const curve positive_fraction{{{0, 0}, {2, 5}}};
	require(positive_fraction.evaluate_as<int>(1, polyline_extrapolation::reject, polyline_integral_rounding::toward_zero) == 2,
		"polyline toward-zero rounding mismatch");
	require(positive_fraction.evaluate_as<int>(1, polyline_extrapolation::reject, polyline_integral_rounding::nearest_away_from_zero) == 3,
		"polyline nearest rounding mismatch");
	require(positive_fraction.evaluate_as<int>(1, polyline_extrapolation::reject, polyline_integral_rounding::floor) == 2,
		"polyline floor rounding mismatch");
	require(positive_fraction.evaluate_as<int>(1, polyline_extrapolation::reject, polyline_integral_rounding::ceil) == 3,
		"polyline ceil rounding mismatch");

	const curve negative_fraction{{{0, 0}, {2, -5}}};
	require(negative_fraction.evaluate_as<int>(1, polyline_extrapolation::reject, polyline_integral_rounding::toward_zero) == -2,
		"negative polyline toward-zero rounding mismatch");
	require(negative_fraction.evaluate_as<int>(1, polyline_extrapolation::reject, polyline_integral_rounding::nearest_away_from_zero) == -3,
		"negative polyline nearest rounding mismatch");
	require(negative_fraction.evaluate_as<int>(1, polyline_extrapolation::reject, polyline_integral_rounding::floor) == -3,
		"negative polyline floor rounding mismatch");
	require(negative_fraction.evaluate_as<int>(1, polyline_extrapolation::reject, polyline_integral_rounding::ceil) == -2,
		"negative polyline ceil rounding mismatch");

	const curve out_of_range{{{0, -1}, {1, 300}}};
	require(!out_of_range.evaluate_as<std::uint8_t>(0), "negative output must not convert to unsigned");
	require(!out_of_range.evaluate_as<std::uint8_t>(1), "large output must not narrow silently");

	using floating_curve = dixelu::polyline_converter<double, double>;
	floating_curve finite;
	require(
		throws<std::invalid_argument>([&] { finite.insert(std::numeric_limits<double>::quiet_NaN(), 1.0); }),
		"polyline must reject a non-finite key");
	require(
		throws<std::invalid_argument>([&] { finite.insert(1.0, std::numeric_limits<double>::infinity()); }),
		"polyline must reject a non-finite value");
	require(value.evaluate_as<double>(0).has_value(), "finite conversion sanity check");
}

void test_lookup_tables()
{
	using dixelu::polyline_extrapolation;
	const dixelu::polyline_converter<int, int> byte_curve{{{0, 0}, {255, 510}}};
	const dixelu::byte_polyline_lookup_table byte_table(byte_curve, polyline_extrapolation::reject);
	require(byte_table[0] == 0 && byte_table[127] == 254, "byte lookup interpolation mismatch");
	require(!byte_table[128] && byte_table.invalid_count() == 128, "byte lookup narrowing must remain invalid");
	require(!byte_table.at(256), "byte lookup out-of-range access must be invalid");
	const auto byte_materialized = byte_table.materialize(0xFF);
	require(byte_materialized[128] == 0xFF, "byte lookup fallback must be explicitly materialized");

	const dixelu::polyline_converter<int, int> partial{{{50, 10}, {200, 20}}};
	const dixelu::byte_polyline_lookup_table clamped(partial, polyline_extrapolation::clamp);
	require(clamped.all_valid() && clamped[0] == 10 && clamped[255] == 20, "clamped lookup boundary mismatch");

	const dixelu::polyline_converter<std::uint16_t, std::uint32_t> midi_curve{{{0, 0}, {0x3FFF, 0x7FFE}}};
	const auto midi_table = dixelu::make_midi14_polyline_lookup_table(
		midi_curve, polyline_extrapolation::reject);
	require(midi_table[0] == 0 && midi_table[8191] == 16'382, "14-bit lookup valid range mismatch");
	require(!midi_table[8192] && midi_table.invalid_count() == 8192, "14-bit lookup must retain invalid values explicitly");
	require(!midi_table.at(1U << 14U), "14-bit lookup out-of-range access must not alias a valid slot");
	const auto legacy_boundary = midi_table.materialize(0x4000);
	require(legacy_boundary[8192] == 0x4000, "14-bit sentinel must be an explicit caller choice");

	require(
		throws<std::invalid_argument>([&] {
			(void)dixelu::polyline_lookup_table<std::uint8_t, 4>(
				byte_curve,
				polyline_extrapolation::clamp,
				dixelu::polyline_integral_rounding::toward_zero,
				std::pair<std::uint8_t, std::uint8_t>{10, 1});
		}),
		"lookup table must reject a reversed valid-output range");
	require(
		throws<std::invalid_argument>([&] {
			(void)dixelu::polyline_lookup_table<double, 4>(
				byte_curve,
				polyline_extrapolation::clamp,
				dixelu::polyline_integral_rounding::toward_zero,
				std::pair{0.0, std::numeric_limits<double>::infinity()});
		}),
		"lookup table must reject a non-finite valid-output range");
}

void test_randomized_reference()
{
	using curve = dixelu::polyline_converter<double, double>;
	const std::array policies{
		dixelu::polyline_extrapolation::reject,
		dixelu::polyline_extrapolation::clamp,
		dixelu::polyline_extrapolation::linear};
	std::mt19937_64 random(0x50171E5ULL);

	for (std::size_t iteration = 0; iteration < 600; ++iteration)
	{
		const std::size_t point_count = 2 + static_cast<std::size_t>(random() % 7U);
		std::vector<reference_point> reference;
		std::vector<curve::point_type> input;
		long double key = static_cast<long double>(static_cast<std::int64_t>(random() % 41U) - 20);
		for (std::size_t index = 0; index < point_count; ++index)
		{
			key += static_cast<long double>(1U + random() % 20U);
			const long double value = static_cast<long double>(static_cast<std::int64_t>(random() % 2001U) - 1000);
			reference.emplace_back(key, value);
			input.emplace_back(static_cast<double>(key), static_cast<double>(value));
		}
		const curve converter(input);
		const long double lower = reference.front().first - 30.0L;
		const long double width = reference.back().first - reference.front().first + 60.0L;
		for (std::size_t query_index = 0; query_index < 50; ++query_index)
		{
			const long double query = lower +
				static_cast<long double>(random() % 100'001U) / 100'000.0L * width;
			const double converted_query = static_cast<double>(query);
			for (const auto policy : policies)
			{
				require_optional_equal(
					converter.evaluate(converted_query, policy),
					reference_evaluate(reference, static_cast<long double>(converted_query), policy),
					"randomized polyline reference mismatch");
			}
		}
	}
}

} // namespace

int main()
{
	test_curve_contracts();
	test_lookup_tables();
	test_randomized_reference();
}
