#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace dixelu
{

template<typename T, std::size_t Dimensions>
class point
{
	static_assert(Dimensions > 0, "point dimensions must be positive");

public:
	using value_type = T;
	using self_type = point<T, Dimensions>;
	using iterator = typename std::array<T, Dimensions>::iterator;
	using const_iterator = typename std::array<T, Dimensions>::const_iterator;

	constexpr point() = default;

	constexpr explicit point(const T& value)
	{
		values_.fill(value);
	}

	constexpr point(std::initializer_list<T> values)
	{
		if (values.size() > Dimensions)
			throw std::length_error("too many point coordinates");
		std::copy(values.begin(), values.end(), values_.begin());
	}

	constexpr explicit point(const std::array<T, Dimensions>& values) : values_(values) {}

	[[nodiscard]] static consteval std::size_t size() noexcept { return Dimensions; }
	[[nodiscard]] constexpr T* data() noexcept { return values_.data(); }
	[[nodiscard]] constexpr const T* data() const noexcept { return values_.data(); }
	[[nodiscard]] constexpr iterator begin() noexcept { return values_.begin(); }
	[[nodiscard]] constexpr const_iterator begin() const noexcept { return values_.begin(); }
	[[nodiscard]] constexpr iterator end() noexcept { return values_.end(); }
	[[nodiscard]] constexpr const_iterator end() const noexcept { return values_.end(); }

	[[nodiscard]] constexpr T& operator[](std::size_t index) noexcept { return values_[index]; }
	[[nodiscard]] constexpr const T& operator[](std::size_t index) const noexcept { return values_[index]; }
	[[nodiscard]] constexpr T& at(std::size_t index) { return values_.at(index); }
	[[nodiscard]] constexpr const T& at(std::size_t index) const { return values_.at(index); }

	constexpr void swap(self_type& other) noexcept(std::is_nothrow_swappable_v<T>)
	{
		values_.swap(other.values_);
	}

	[[nodiscard]] constexpr self_type operator+(const self_type& other) const
	{
		self_type result(*this);
		return result += other;
	}

	[[nodiscard]] constexpr self_type operator-(const self_type& other) const
	{
		self_type result(*this);
		return result -= other;
	}

	constexpr self_type& operator+=(const self_type& other)
	{
		for (std::size_t index = 0; index < Dimensions; ++index)
			values_[index] += other.values_[index];
		return *this;
	}

	constexpr self_type& operator-=(const self_type& other)
	{
		for (std::size_t index = 0; index < Dimensions; ++index)
			values_[index] -= other.values_[index];
		return *this;
	}

	[[nodiscard]] constexpr self_type operator*(const T& scalar) const
	{
		self_type result(*this);
		return result *= scalar;
	}

	constexpr self_type& operator*=(const T& scalar)
	{
		for (T& value : values_)
			value *= scalar;
		return *this;
	}

	[[nodiscard]] constexpr self_type operator/(const T& scalar) const
	{
		self_type result(*this);
		return result /= scalar;
	}

	constexpr self_type& operator/=(const T& scalar)
	{
		if (scalar == T())
			throw std::domain_error("point scalar division by zero");
		for (T& value : values_)
			value /= scalar;
		return *this;
	}

	[[nodiscard]] constexpr self_type operator-() const
	{
		self_type result;
		for (std::size_t index = 0; index < Dimensions; ++index)
			result[index] = -values_[index];
		return result;
	}

	[[nodiscard]] constexpr T dot(const self_type& other) const
	{
		T result{};
		for (std::size_t index = 0; index < Dimensions; ++index)
			result += values_[index] * other.values_[index];
		return result;
	}

	[[nodiscard]] constexpr T operator*(const self_type& other) const { return dot(other); }

	[[nodiscard]] constexpr T get_norm2() const { return dot(*this); }

	[[nodiscard]] T get_norm(T exponent = T(2)) const
	{
		if (!(exponent > T()))
			throw std::domain_error("point norm exponent must be positive");
		long double sum = 0;
		for (const T& value : values_)
			sum += std::pow(static_cast<long double>(absolute(value)), static_cast<long double>(exponent));
		return static_cast<T>(std::pow(sum, 1.0L / static_cast<long double>(exponent)));
	}

	[[nodiscard]] self_type normalize(T exponent = T(2)) const
	{
		const T length = get_norm(exponent);
		if (length == T())
			throw std::domain_error("cannot normalize a zero point");
		return *this / length;
	}

	[[nodiscard]] constexpr bool operator==(const self_type& other) const = default;

	[[nodiscard]] constexpr bool operator<(const self_type& other) const
	{
		return std::ranges::equal(values_, other.values_, std::less<>{});
	}

	[[nodiscard]] constexpr bool operator<=(const self_type& other) const
	{
		return std::ranges::equal(values_, other.values_, std::less_equal<>{});
	}

private:
	std::array<T, Dimensions> values_{};

	[[nodiscard]] static constexpr T absolute(T value)
	{
		if constexpr (std::is_unsigned_v<T>)
			return value;
		else if constexpr (std::integral<T>)
		{
			if (value == (std::numeric_limits<T>::lowest)())
				throw std::overflow_error("point absolute value is not representable");
			return value < T() ? -value : value;
		}
		else
		{
			using std::abs;
			return abs(value);
		}
	}
};

template<typename T, std::size_t Dimensions>
[[nodiscard]] constexpr point<T, Dimensions> operator*(const T& scalar, const point<T, Dimensions>& value)
{
	return value * scalar;
}

template<typename T, std::size_t Dimensions>
std::ostream& operator<<(std::ostream& output, const point<T, Dimensions>& value)
{
	output << '(';
	for (std::size_t index = 0; index < Dimensions; ++index)
	{
		if (index != 0)
			output << ',';
		output << value[index];
	}
	return output << ')';
}

template<typename T, std::size_t Dimensions>
class sq_matrix
{
	static_assert(Dimensions > 0, "square matrix dimensions must be positive");

public:
	using value_type = T;
	using point_type = point<T, Dimensions>;
	using self_type = sq_matrix<T, Dimensions>;
	static constexpr std::size_t dimensions = Dimensions;
	static constexpr std::size_t minor_type_size = Dimensions > 1 ? Dimensions - 1 : 1;
	using minor_type = sq_matrix<T, minor_type_size>;

	constexpr sq_matrix() = default;

	constexpr explicit sq_matrix(const T& diagonal_value)
	{
		for (std::size_t index = 0; index < Dimensions; ++index)
			rows_[index][index] = diagonal_value;
	}

	constexpr sq_matrix(std::initializer_list<point_type> rows)
	{
		if (rows.size() > Dimensions)
			throw std::length_error("too many square-matrix rows");
		std::copy(rows.begin(), rows.end(), rows_.begin());
	}

	constexpr explicit sq_matrix(const std::array<point_type, Dimensions>& rows) : rows_(rows) {}

	[[nodiscard]] static constexpr self_type identity() { return self_type(T(1)); }
	[[nodiscard]] static consteval std::size_t size() noexcept { return Dimensions; }

	[[nodiscard]] constexpr point_type& operator[](std::size_t row) noexcept { return rows_[row]; }
	[[nodiscard]] constexpr const point_type& operator[](std::size_t row) const noexcept { return rows_[row]; }

	[[nodiscard]] constexpr T& operator()(std::size_t row, std::size_t column) noexcept
	{
		return rows_[row][column];
	}

	[[nodiscard]] constexpr const T& operator()(std::size_t row, std::size_t column) const noexcept
	{
		return rows_[row][column];
	}

	[[nodiscard]] constexpr T& at(std::size_t row, std::size_t column)
	{
		if (row >= Dimensions || column >= Dimensions)
			throw std::out_of_range("square-matrix coordinate is out of range");
		return (*this)(row, column);
	}

	[[nodiscard]] constexpr const T& at(std::size_t row, std::size_t column) const
	{
		if (row >= Dimensions || column >= Dimensions)
			throw std::out_of_range("square-matrix coordinate is out of range");
		return (*this)(row, column);
	}

	constexpr void swap(self_type& other) noexcept(std::is_nothrow_swappable_v<T>)
	{
		rows_.swap(other.rows_);
	}

	[[nodiscard]] constexpr self_type operator+(const self_type& other) const
	{
		self_type result(*this);
		return result += other;
	}

	[[nodiscard]] constexpr self_type operator-(const self_type& other) const
	{
		self_type result(*this);
		return result -= other;
	}

	constexpr self_type& operator+=(const self_type& other)
	{
		for (std::size_t row = 0; row < Dimensions; ++row)
			rows_[row] += other.rows_[row];
		return *this;
	}

	constexpr self_type& operator-=(const self_type& other)
	{
		for (std::size_t row = 0; row < Dimensions; ++row)
			rows_[row] -= other.rows_[row];
		return *this;
	}

	[[nodiscard]] constexpr self_type operator*(const T& scalar) const
	{
		self_type result(*this);
		return result *= scalar;
	}

	constexpr self_type& operator*=(const T& scalar)
	{
		for (point_type& row : rows_)
			row *= scalar;
		return *this;
	}

	[[nodiscard]] constexpr self_type operator/(const T& scalar) const
	{
		self_type result(*this);
		return result /= scalar;
	}

	constexpr self_type& operator/=(const T& scalar)
	{
		if (scalar == T())
			throw std::domain_error("square-matrix scalar division by zero");
		for (point_type& row : rows_)
			row /= scalar;
		return *this;
	}

	[[nodiscard]] constexpr point_type operator*(const point_type& vector) const
	{
		point_type result;
		for (std::size_t row = 0; row < Dimensions; ++row)
			result[row] = rows_[row].dot(vector);
		return result;
	}

	[[nodiscard]] constexpr self_type operator*(const self_type& other) const
	{
		self_type result;
		for (std::size_t row = 0; row < Dimensions; ++row)
		{
			for (std::size_t inner = 0; inner < Dimensions; ++inner)
			{
				const T left = (*this)(row, inner);
				for (std::size_t column = 0; column < Dimensions; ++column)
					result(row, column) += left * other(inner, column);
			}
		}
		return result;
	}

	[[nodiscard]] constexpr self_type operator^(std::int64_t exponent) const
	{
		self_type base(*this);
		std::uint64_t magnitude;
		if (exponent < 0)
		{
			if constexpr (std::floating_point<T>)
				base = inverse();
			else
				throw std::domain_error("negative square-matrix power requires floating-point elements");
			magnitude = static_cast<std::uint64_t>(-(exponent + 1)) + 1U;
		}
		else
		{
			magnitude = static_cast<std::uint64_t>(exponent);
		}

		self_type result = identity();
		while (magnitude != 0)
		{
			if ((magnitude & 1U) != 0)
				result = result * base;
			magnitude >>= 1U;
			if (magnitude != 0)
				base = base * base;
		}
		return result;
	}

	constexpr self_type& operator^=(std::int64_t exponent)
	{
		return *this = *this ^ exponent;
	}

	[[nodiscard]] constexpr self_type transposed() const
	{
		self_type result;
		for (std::size_t row = 0; row < Dimensions; ++row)
			for (std::size_t column = 0; column < Dimensions; ++column)
				result(column, row) = (*this)(row, column);
		return result;
	}

	constexpr self_type& transpose()
	{
		for (std::size_t row = 0; row < Dimensions; ++row)
			for (std::size_t column = row + 1; column < Dimensions; ++column)
				std::swap((*this)(row, column), (*this)(column, row));
		return *this;
	}

	[[nodiscard]] constexpr T trace() const
	{
		T result{};
		for (std::size_t index = 0; index < Dimensions; ++index)
			result += (*this)(index, index);
		return result;
	}

	[[nodiscard]] constexpr T determinant(T tolerance = default_tolerance()) const
		requires std::floating_point<T>
	{
		check_tolerance(tolerance);
		self_type work(*this);
		T result(1);
		for (std::size_t pivot_column = 0; pivot_column < Dimensions; ++pivot_column)
		{
			const std::size_t pivot_row = work.find_pivot(pivot_column, pivot_column);
			if (absolute(work(pivot_row, pivot_column)) <= tolerance)
				return T();
			if (pivot_row != pivot_column)
			{
				work.rows_[pivot_row].swap(work.rows_[pivot_column]);
				result = -result;
			}
			const T pivot = work(pivot_column, pivot_column);
			result *= pivot;
			for (std::size_t row = pivot_column + 1; row < Dimensions; ++row)
			{
				const T factor = work(row, pivot_column) / pivot;
				work(row, pivot_column) = T();
				for (std::size_t column = pivot_column + 1; column < Dimensions; ++column)
					work(row, column) -= factor * work(pivot_column, column);
			}
		}
		return result;
	}

	[[nodiscard]] constexpr self_type inverse(T tolerance = default_tolerance()) const
		requires std::floating_point<T>
	{
		check_tolerance(tolerance);
		self_type left(*this);
		self_type right = identity();
		gauss_jordan(left, right, tolerance);
		return right;
	}

	[[nodiscard]] constexpr point_type solve(point_type right_hand_side, T tolerance = default_tolerance()) const
		requires std::floating_point<T>
	{
		check_tolerance(tolerance);
		self_type left(*this);
		for (std::size_t pivot_column = 0; pivot_column < Dimensions; ++pivot_column)
		{
			const std::size_t pivot_row = left.find_pivot(pivot_column, pivot_column);
			if (absolute(left(pivot_row, pivot_column)) <= tolerance)
				throw std::domain_error("square matrix is singular");
			left.rows_[pivot_row].swap(left.rows_[pivot_column]);
			std::swap(right_hand_side[pivot_row], right_hand_side[pivot_column]);
			const T pivot = left(pivot_column, pivot_column);
			for (std::size_t column = 0; column < Dimensions; ++column)
				left(pivot_column, column) /= pivot;
			right_hand_side[pivot_column] /= pivot;
			for (std::size_t row = 0; row < Dimensions; ++row)
			{
				if (row == pivot_column)
					continue;
				const T factor = left(row, pivot_column);
				left(row, pivot_column) = T();
				for (std::size_t column = pivot_column + 1; column < Dimensions; ++column)
					left(row, column) -= factor * left(pivot_column, column);
				right_hand_side[row] -= factor * right_hand_side[pivot_column];
			}
		}
		return right_hand_side;
	}

	[[nodiscard]] static constexpr point_type solve_using_eulers_method(self_type coefficients, point_type right_hand_side)
		requires std::floating_point<T>
	{
		return coefficients.solve(right_hand_side);
	}

	[[nodiscard]] constexpr minor_type minor_matrix(std::size_t removed_row, std::size_t removed_column) const
	{
		if (removed_row >= Dimensions || removed_column >= Dimensions)
			throw std::out_of_range("square-matrix minor coordinate is out of range");
		minor_type result;
		if constexpr (Dimensions > 1)
		{
			for (std::size_t source_row = 0, target_row = 0; source_row < Dimensions; ++source_row)
			{
				if (source_row == removed_row)
					continue;
				for (std::size_t source_column = 0, target_column = 0; source_column < Dimensions; ++source_column)
				{
					if (source_column == removed_column)
						continue;
					result(target_row, target_column++) = (*this)(source_row, source_column);
				}
				++target_row;
			}
		}
		return result;
	}

	template<std::size_t NewDimensions>
	[[nodiscard]] constexpr sq_matrix<T, NewDimensions> to(std::size_t start_index = 0) const
	{
		if (start_index > Dimensions)
			throw std::out_of_range("square-matrix block start is out of range");
		sq_matrix<T, NewDimensions> result;
		const std::size_t copied = (std::min)(NewDimensions, Dimensions - start_index);
		for (std::size_t row = 0; row < copied; ++row)
			for (std::size_t column = 0; column < copied; ++column)
				result(row, column) = (*this)(row + start_index, column + start_index);
		return result;
	}

	[[nodiscard]] self_type ppow(const T& exponent) const
	{
		self_type result(*this);
		return result.selfppow(exponent);
	}

	self_type& selfppow(const T& exponent)
	{
		for (point_type& row : rows_)
			for (T& value : row)
				value = static_cast<T>(std::pow(value, exponent));
		return *this;
	}

	[[nodiscard]] constexpr self_type pabs() const
	{
		self_type result(*this);
		return result.selfpabs();
	}

	constexpr self_type& selfpabs()
	{
		for (point_type& row : rows_)
			for (T& value : row)
				value = absolute(value);
		return *this;
	}

	[[nodiscard]] constexpr T psum() const
	{
		T result{};
		for (const point_type& row : rows_)
			for (const T& value : row)
				result += value;
		return result;
	}

	template<typename Function>
	constexpr self_type& selfapply(Function&& function)
	{
		for (point_type& row : rows_)
			for (T& value : row)
				std::invoke(function, value);
		return *this;
	}

	template<typename Function>
	[[nodiscard]] constexpr self_type apply(Function&& function) const
	{
		self_type result(*this);
		result.selfapply(std::forward<Function>(function));
		return result;
	}

	template<typename Function>
	constexpr self_type& selfapply_indexed(Function&& function)
	{
		for (std::size_t row = 0; row < Dimensions; ++row)
			for (std::size_t column = 0; column < Dimensions; ++column)
				std::invoke(function, (*this)(row, column), row, column);
		return *this;
	}

	template<typename Function>
	[[nodiscard]] constexpr self_type apply_indexed(Function&& function) const
	{
		self_type result(*this);
		result.selfapply_indexed(std::forward<Function>(function));
		return result;
	}

	[[nodiscard]] std::pair<T, T> minmax() const
	{
		bool found = false;
		T minimum{};
		T maximum{};
		for (const point_type& row : rows_)
		{
			for (const T& value : row)
			{
				if constexpr (std::floating_point<T>)
				{
					if (!std::isfinite(value))
						continue;
				}
				if (!found)
				{
					minimum = value;
					maximum = value;
					found = true;
				}
				else
				{
					minimum = (std::min)(minimum, value);
					maximum = (std::max)(maximum, value);
				}
			}
		}
		if (!found)
			throw std::domain_error("square matrix has no finite values");
		return {minimum, maximum};
	}

	[[nodiscard]] constexpr bool operator==(const self_type& other) const = default;

	[[nodiscard]] static constexpr T default_tolerance() noexcept
	{
		if constexpr (std::floating_point<T>)
			return std::numeric_limits<T>::epsilon() * T(64);
		else
			return T();
	}

private:
	std::array<point_type, Dimensions> rows_{};

	[[nodiscard]] static constexpr T absolute(T value)
	{
		if constexpr (std::is_unsigned_v<T>)
			return value;
		else if constexpr (std::integral<T>)
		{
			if (value == (std::numeric_limits<T>::lowest)())
				throw std::overflow_error("square-matrix absolute value is not representable");
			return value < T() ? -value : value;
		}
		else
		{
			using std::abs;
			return abs(value);
		}
	}

	static constexpr void check_tolerance(T tolerance)
		requires std::floating_point<T>
	{
		if (!std::isfinite(tolerance) || tolerance < T())
			throw std::invalid_argument("square-matrix tolerance must be finite and nonnegative");
	}

	[[nodiscard]] constexpr std::size_t find_pivot(std::size_t first_row, std::size_t column) const
	{
		std::size_t pivot = first_row;
		for (std::size_t row = first_row + 1; row < Dimensions; ++row)
		{
			if (absolute((*this)(pivot, column)) < absolute((*this)(row, column)))
				pivot = row;
		}
		return pivot;
	}

	static constexpr void gauss_jordan(self_type& left, self_type& right, T tolerance)
		requires std::floating_point<T>
	{
		for (std::size_t pivot_column = 0; pivot_column < Dimensions; ++pivot_column)
		{
			const std::size_t pivot_row = left.find_pivot(pivot_column, pivot_column);
			if (absolute(left(pivot_row, pivot_column)) <= tolerance)
				throw std::domain_error("square matrix is singular");
			left.rows_[pivot_row].swap(left.rows_[pivot_column]);
			right.rows_[pivot_row].swap(right.rows_[pivot_column]);
			const T pivot = left(pivot_column, pivot_column);
			left.rows_[pivot_column] /= pivot;
			right.rows_[pivot_column] /= pivot;
			for (std::size_t row = 0; row < Dimensions; ++row)
			{
				if (row == pivot_column)
					continue;
				const T factor = left(row, pivot_column);
				left(row, pivot_column) = T();
				for (std::size_t column = pivot_column + 1; column < Dimensions; ++column)
					left(row, column) -= factor * left(pivot_column, column);
				for (std::size_t column = 0; column < Dimensions; ++column)
					right(row, column) -= factor * right(pivot_column, column);
			}
		}
	}
};

template<typename T, std::size_t Dimensions>
using fixed_matrix = sq_matrix<T, Dimensions>;

template<typename T, std::size_t Dimensions>
[[nodiscard]] constexpr sq_matrix<T, Dimensions> operator*(const T& scalar, const sq_matrix<T, Dimensions>& value)
{
	return value * scalar;
}

template<typename T, std::size_t Dimensions>
std::ostream& operator<<(std::ostream& output, const sq_matrix<T, Dimensions>& value)
{
	for (std::size_t row = 0; row < Dimensions; ++row)
	{
		for (std::size_t column = 0; column < Dimensions; ++column)
		{
			if (column != 0)
				output << ' ';
			output << value(row, column);
		}
		output << '\n';
	}
	return output;
}

template<typename T, std::size_t Dimensions>
[[nodiscard]] constexpr point<T, Dimensions> cross_prod(
	const std::array<point<T, Dimensions>, sq_matrix<T, Dimensions>::minor_type_size>& points)
	requires std::floating_point<T>
{
	point<T, Dimensions> result;
	if constexpr (Dimensions == 1)
	{
		return result;
	}
	else
	{
		sq_matrix<T, Dimensions> expanded;
		for (std::size_t row = 0; row < Dimensions - 1; ++row)
			expanded[row + 1] = points[row];
		for (std::size_t column = 0; column < Dimensions; ++column)
		{
			const T cofactor = expanded.minor_matrix(0, column).determinant();
			result[column] = (column & 1U) == 0 ? cofactor : -cofactor;
		}
		return result;
	}
}

} // namespace dixelu
