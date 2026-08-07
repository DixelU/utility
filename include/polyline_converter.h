#pragma once

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace dixelu
{

template<typename T>
concept polyline_number =
	std::is_arithmetic_v<T> && !std::same_as<std::remove_cv_t<T>, bool>;

enum class polyline_extrapolation
{
	reject,
	clamp,
	linear
};

enum class polyline_integral_rounding
{
	toward_zero,
	nearest_away_from_zero,
	floor,
	ceil
};

enum class polyline_point_update
{
	inserted,
	replaced,
	unchanged
};

template<polyline_number Key, polyline_number Value, std::floating_point Result = long double>
class polyline_converter
{
public:
	using key_type = Key;
	using value_type = Value;
	using result_type = Result;
	using point_type = std::pair<Key, Value>;
	using map_type = std::map<Key, Value>;

	polyline_converter() = default;

	polyline_converter(std::initializer_list<point_type> points)
	{
		assign(points.begin(), points.end());
	}

	explicit polyline_converter(const std::vector<point_type>& points)
	{
		assign(points.begin(), points.end());
	}

	[[nodiscard]] std::size_t size() const noexcept { return points_.size(); }
	[[nodiscard]] bool empty() const noexcept { return points_.empty(); }
	[[nodiscard]] const map_type& points() const noexcept { return points_; }

	void clear() noexcept { points_.clear(); }

	[[nodiscard]] bool erase(const Key& key) { return points_.erase(key) != 0; }

	polyline_point_update insert_or_assign(const Key& key, const Value& value)
	{
		validate_point(key, value);
		const auto existing = points_.find(key);
		if (existing == points_.end())
		{
			points_.emplace(key, value);
			return polyline_point_update::inserted;
		}
		if (existing->second == value)
			return polyline_point_update::unchanged;
		existing->second = value;
		return polyline_point_update::replaced;
	}

	void insert(const Key& key, const Value& value)
	{
		(void)insert_or_assign(key, value);
	}

	[[nodiscard]] std::optional<Result> evaluate(
		const Key& key,
		polyline_extrapolation extrapolation = polyline_extrapolation::reject) const
	{
		if (!is_finite(key) || points_.empty())
			return std::nullopt;

		const auto right = points_.lower_bound(key);
		if (right != points_.end() && right->first == key)
			return checked_result(right->second);

		if (points_.size() == 1)
		{
			if (extrapolation == polyline_extrapolation::reject)
				return std::nullopt;
			return checked_result(points_.begin()->second);
		}

		if (right == points_.begin())
		{
			if (extrapolation == polyline_extrapolation::reject)
				return std::nullopt;
			if (extrapolation == polyline_extrapolation::clamp)
				return checked_result(right->second);
			return interpolate(key, right, std::next(right));
		}

		if (right == points_.end())
		{
			const auto last = std::prev(points_.end());
			if (extrapolation == polyline_extrapolation::reject)
				return std::nullopt;
			if (extrapolation == polyline_extrapolation::clamp)
				return checked_result(last->second);
			return interpolate(key, std::prev(last), last);
		}

		return interpolate(key, std::prev(right), right);
	}

	[[nodiscard]] std::optional<Result> operator()(
		const Key& key,
		polyline_extrapolation extrapolation = polyline_extrapolation::reject) const
	{
		return evaluate(key, extrapolation);
	}

	[[nodiscard]] Result at(
		const Key& key,
		polyline_extrapolation extrapolation = polyline_extrapolation::reject) const
	{
		const auto result = evaluate(key, extrapolation);
		if (!result)
			throw std::out_of_range("polyline query has no valid result");
		return *result;
	}

	template<polyline_number Output>
	[[nodiscard]] std::optional<Output> evaluate_as(
		const Key& key,
		polyline_extrapolation extrapolation = polyline_extrapolation::reject,
		polyline_integral_rounding rounding = polyline_integral_rounding::toward_zero) const
	{
		const auto result = evaluate(key, extrapolation);
		if (!result)
			return std::nullopt;

		long double converted = static_cast<long double>(*result);
		if constexpr (std::integral<Output>)
		{
			switch (rounding)
			{
			case polyline_integral_rounding::toward_zero:
				converted = std::trunc(converted);
				break;
			case polyline_integral_rounding::nearest_away_from_zero:
				converted = std::round(converted);
				break;
			case polyline_integral_rounding::floor:
				converted = std::floor(converted);
				break;
			case polyline_integral_rounding::ceil:
				converted = std::ceil(converted);
				break;
			}

			static_assert(std::numeric_limits<Output>::radix == 2);
			const long double upper_exclusive =
				std::ldexp(1.0L, std::numeric_limits<Output>::digits);
			const long double lower_inclusive =
				std::is_unsigned_v<Output> ? 0.0L : -upper_exclusive;
			if (converted < lower_inclusive || converted >= upper_exclusive)
				return std::nullopt;
		}
		else
		{
			const long double lowest = static_cast<long double>((std::numeric_limits<Output>::lowest)());
			const long double maximum = static_cast<long double>((std::numeric_limits<Output>::max)());
			if (converted < lowest || converted > maximum)
				return std::nullopt;
		}

		const Output output = static_cast<Output>(converted);
		if constexpr (std::floating_point<Output>)
		{
			if (!std::isfinite(output))
				return std::nullopt;
		}
		return output;
	}

private:
	map_type points_;

	template<typename Iterator>
	void assign(Iterator first, Iterator last)
	{
		for (Iterator current = first; current != last; ++current)
			(void)insert_or_assign(current->first, current->second);
	}

	static void validate_point(const Key& key, const Value& value)
	{
		if (!is_finite(key) || !is_finite(value))
			throw std::invalid_argument("polyline points must be finite");
	}

	template<polyline_number Number>
	[[nodiscard]] static bool is_finite(const Number& value)
	{
		if constexpr (std::floating_point<Number>)
			return std::isfinite(value);
		else
			return true;
	}

	[[nodiscard]] static std::optional<Result> checked_result(const Value& value)
	{
		const Result result = static_cast<Result>(value);
		if (!std::isfinite(result))
			return std::nullopt;
		return result;
	}

	[[nodiscard]] static std::optional<Result> interpolate(
		const Key& key,
		typename map_type::const_iterator left,
		typename map_type::const_iterator right)
	{
		const Result left_key = static_cast<Result>(left->first);
		const Result right_key = static_cast<Result>(right->first);
		const Result left_value = static_cast<Result>(left->second);
		const Result right_value = static_cast<Result>(right->second);
		const Result ratio = (static_cast<Result>(key) - left_key) / (right_key - left_key);
		const Result result = left_value + ratio * (right_value - left_value);
		if (!std::isfinite(result))
			return std::nullopt;
		return result;
	}
};

template<polyline_number Output, std::size_t Size>
class polyline_lookup_table
{
public:
	using output_type = Output;
	using entry_type = std::optional<Output>;
	static constexpr std::size_t size = Size;

	template<polyline_number Key, polyline_number Value, std::floating_point Result>
	explicit polyline_lookup_table(
		const polyline_converter<Key, Value, Result>& converter,
		polyline_extrapolation extrapolation,
		polyline_integral_rounding rounding = polyline_integral_rounding::toward_zero,
		std::optional<std::pair<Output, Output>> valid_output_range = std::nullopt)
	{
		if constexpr (std::floating_point<Output>)
		{
			if (valid_output_range &&
				(!std::isfinite(valid_output_range->first) || !std::isfinite(valid_output_range->second)))
			{
				throw std::invalid_argument("polyline lookup output range must be finite");
			}
		}
		if (valid_output_range && valid_output_range->second < valid_output_range->first)
			throw std::invalid_argument("polyline lookup output range is reversed");

		for (std::size_t index = 0; index < Size; ++index)
		{
			if constexpr (std::integral<Key>)
			{
				if (index > static_cast<std::uintmax_t>((std::numeric_limits<Key>::max)()))
					continue;
			}

			entry_type entry = converter.template evaluate_as<Output>(
				static_cast<Key>(index), extrapolation, rounding);
			if (entry && valid_output_range &&
				(*entry < valid_output_range->first || valid_output_range->second < *entry))
			{
				entry.reset();
			}
			entries_[index] = entry;
		}
	}

	[[nodiscard]] const entry_type& operator[](std::size_t index) const noexcept
	{
		return entries_[index];
	}

	[[nodiscard]] entry_type at(std::size_t index) const noexcept
	{
		return index < Size ? entries_[index] : std::nullopt;
	}

	[[nodiscard]] std::size_t invalid_count() const noexcept
	{
		std::size_t result = 0;
		for (const entry_type& entry : entries_)
			result += entry.has_value() ? 0U : 1U;
		return result;
	}

	[[nodiscard]] bool all_valid() const noexcept { return invalid_count() == 0; }

	[[nodiscard]] std::array<Output, Size> materialize(const Output& invalid_fallback) const
	{
		std::array<Output, Size> result{};
		for (std::size_t index = 0; index < Size; ++index)
			result[index] = entries_[index].value_or(invalid_fallback);
		return result;
	}

private:
	std::array<entry_type, Size> entries_{};
};

using byte_polyline_lookup_table = polyline_lookup_table<std::uint8_t, 256>;
using midi14_polyline_lookup_table = polyline_lookup_table<std::uint16_t, 1U << 14U>;

template<polyline_number Key, polyline_number Value, std::floating_point Result>
[[nodiscard]] midi14_polyline_lookup_table make_midi14_polyline_lookup_table(
	const polyline_converter<Key, Value, Result>& converter,
	polyline_extrapolation extrapolation,
	polyline_integral_rounding rounding = polyline_integral_rounding::toward_zero)
{
	return midi14_polyline_lookup_table(
		converter,
		extrapolation,
		rounding,
		std::pair<std::uint16_t, std::uint16_t>{std::uint16_t{0}, std::uint16_t{0x3FFF}});
}

} // namespace dixelu
