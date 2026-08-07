#pragma once

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <initializer_list>
#include <istream>
#include <iterator>
#include <limits>
#include <ostream>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace dixelu
{

// A row-major, dynamically-sized matrix. Coordinates are always (row, column).
template<typename T>
class matrix
{
public:
	using value_type = T;
	using line = std::vector<T>;
	using size_type = std::size_t;
	using self_type = matrix<T>;

	class row_view
	{
	public:
		[[nodiscard]] T& operator[](size_type column) const noexcept { return data_[column]; }
		[[nodiscard]] T& at(size_type column) const
		{
			if (column >= size_)
				throw std::out_of_range("matrix column is out of range");
			return data_[column];
		}
		[[nodiscard]] operator std::span<T>() const noexcept { return {data_, size_}; }

	private:
		friend class matrix;
		constexpr row_view(T* data, size_type size) noexcept : data_(data), size_(size) {}
		T* data_;
		size_type size_;
	};

	class const_row_view
	{
	public:
		[[nodiscard]] const T& operator[](size_type column) const noexcept { return data_[column]; }
		[[nodiscard]] const T& at(size_type column) const
		{
			if (column >= size_)
				throw std::out_of_range("matrix column is out of range");
			return data_[column];
		}
		[[nodiscard]] operator std::span<const T>() const noexcept { return {data_, size_}; }

	private:
		friend class matrix;
		constexpr const_row_view(const T* data, size_type size) noexcept : data_(data), size_(size) {}
		const T* data_;
		size_type size_;
	};

	matrix() = default;

	explicit matrix(size_type size) : matrix(size, size) {}

	matrix(size_type rows, size_type columns, const T& value = T())
		: rows_(rows), columns_(columns), values_(checked_area(rows, columns), value)
	{
	}

	matrix(std::initializer_list<line> rows)
	{
		assign_rows(rows.begin(), rows.end());
	}

	explicit matrix(const std::vector<line>& rows)
	{
		assign_rows(rows.begin(), rows.end());
	}

	[[nodiscard]] static self_type identity(size_type size)
	{
		self_type result(size, size);
		for (size_type index = 0; index < size; ++index)
			result(index, index) = T(1);
		return result;
	}

	[[nodiscard]] static self_type E_matrix(size_type size) { return identity(size); }

	[[nodiscard]] static self_type diagonal(std::span<const T> diagonal_values)
	{
		self_type result(diagonal_values.size(), diagonal_values.size());
		for (size_type index = 0; index < diagonal_values.size(); ++index)
			result(index, index) = diagonal_values[index];
		return result;
	}

	[[nodiscard]] static self_type Diagonal(const line& diagonal_values)
	{
		return diagonal(diagonal_values);
	}

	[[nodiscard]] size_type rows() const noexcept { return rows_; }
	[[nodiscard]] size_type cols() const noexcept { return columns_; }
	[[nodiscard]] std::pair<size_type, size_type> size() const noexcept { return {rows_, columns_}; }
	[[nodiscard]] bool empty() const noexcept { return rows_ == 0 || columns_ == 0; }
	[[nodiscard]] std::span<T> data() noexcept { return values_; }
	[[nodiscard]] std::span<const T> data() const noexcept { return values_; }

	void resize(size_type new_rows, size_type new_columns, const T& value = T())
	{
		std::vector<T> resized(checked_area(new_rows, new_columns), value);
		const size_type copied_rows = (std::min)(rows_, new_rows);
		const size_type copied_columns = (std::min)(columns_, new_columns);
		for (size_type row = 0; row < copied_rows; ++row)
		{
			std::copy_n(
				values_.begin() + static_cast<std::ptrdiff_t>(row * columns_),
				copied_columns,
				resized.begin() + static_cast<std::ptrdiff_t>(row * new_columns));
		}
		rows_ = new_rows;
		columns_ = new_columns;
		values_.swap(resized);
	}

	void swap(self_type& other) noexcept
	{
		using std::swap;
		swap(rows_, other.rows_);
		swap(columns_, other.columns_);
		values_.swap(other.values_);
	}

	[[nodiscard]] T& at(size_type row, size_type column)
	{
		check_coordinate(row, column);
		return (*this)(row, column);
	}

	[[nodiscard]] const T& at(size_type row, size_type column) const
	{
		check_coordinate(row, column);
		return (*this)(row, column);
	}

	[[nodiscard]] T& operator()(size_type row, size_type column) noexcept
	{
		return values_[row * columns_ + column];
	}

	[[nodiscard]] const T& operator()(size_type row, size_type column) const noexcept
	{
		return values_[row * columns_ + column];
	}

	[[nodiscard]] row_view operator[](size_type row) noexcept
	{
		return row_view(values_.data() + row * columns_, columns_);
	}

	[[nodiscard]] const_row_view operator[](size_type row) const noexcept
	{
		return const_row_view(values_.data() + row * columns_, columns_);
	}

	[[nodiscard]] self_type get_row(size_type row) const
	{
		if (row >= rows_)
			throw std::out_of_range("matrix row is out of range");
		self_type result(1, columns_);
		std::copy_n(values_.begin() + static_cast<std::ptrdiff_t>(row * columns_), columns_, result.values_.begin());
		return result;
	}

	[[nodiscard]] self_type get_col(size_type column) const
	{
		if (column >= columns_)
			throw std::out_of_range("matrix column is out of range");
		self_type result(rows_, 1);
		for (size_type row = 0; row < rows_; ++row)
			result(row, 0) = (*this)(row, column);
		return result;
	}

	self_type& set_row(size_type row, const self_type& source)
	{
		if (row >= rows_)
			throw std::out_of_range("matrix row is out of range");
		if (source.rows_ != 1 || source.columns_ != columns_)
			throw std::invalid_argument("matrix row shape mismatch");
		std::copy(source.values_.begin(), source.values_.end(), values_.begin() + static_cast<std::ptrdiff_t>(row * columns_));
		return *this;
	}

	self_type& set_col(size_type column, const self_type& source)
	{
		if (column >= columns_)
			throw std::out_of_range("matrix column is out of range");
		if (source.rows_ != rows_ || source.columns_ != 1)
			throw std::invalid_argument("matrix column shape mismatch");
		for (size_type row = 0; row < rows_; ++row)
			(*this)(row, column) = source(row, 0);
		return *this;
	}

	[[nodiscard]] self_type operator*(const T& scalar) const
	{
		self_type result(*this);
		result *= scalar;
		return result;
	}

	self_type& operator*=(const T& scalar)
	{
		for (T& value : values_)
			value *= scalar;
		return *this;
	}

	[[nodiscard]] self_type operator/(const T& scalar) const
	{
		self_type result(*this);
		result /= scalar;
		return result;
	}

	self_type& operator/=(const T& scalar)
	{
		if (scalar == T())
			throw std::domain_error("matrix scalar division by zero");
		for (T& value : values_)
			value /= scalar;
		return *this;
	}

	[[nodiscard]] self_type operator+(const self_type& other) const
	{
		self_type result(*this);
		result += other;
		return result;
	}

	[[nodiscard]] self_type operator-(const self_type& other) const
	{
		self_type result(*this);
		result -= other;
		return result;
	}

	self_type& operator+=(const self_type& other)
	{
		check_same_shape(other, "matrix addition shape mismatch");
		for (size_type index = 0; index < values_.size(); ++index)
			values_[index] += other.values_[index];
		return *this;
	}

	self_type& operator-=(const self_type& other)
	{
		check_same_shape(other, "matrix subtraction shape mismatch");
		for (size_type index = 0; index < values_.size(); ++index)
			values_[index] -= other.values_[index];
		return *this;
	}

	[[nodiscard]] self_type operator*(const self_type& other) const
	{
		if (columns_ != other.rows_)
			throw std::invalid_argument("matrix multiplication shape mismatch");
		self_type result(rows_, other.columns_);
		for (size_type row = 0; row < rows_; ++row)
		{
			for (size_type inner = 0; inner < columns_; ++inner)
			{
				const T left = (*this)(row, inner);
				for (size_type column = 0; column < other.columns_; ++column)
					result(row, column) += left * other(inner, column);
			}
		}
		return result;
	}

	[[nodiscard]] self_type operator^(std::int64_t exponent) const
	{
		if (rows_ != columns_)
			throw std::invalid_argument("matrix power requires a square matrix");
		self_type base(*this);
		std::uint64_t magnitude;
		if (exponent < 0)
		{
			if constexpr (std::floating_point<T>)
				base = inverse();
			else
				throw std::domain_error("negative matrix power requires floating-point elements");
			magnitude = static_cast<std::uint64_t>(-(exponent + 1)) + 1U;
		}
		else
		{
			magnitude = static_cast<std::uint64_t>(exponent);
		}

		self_type result = identity(rows_);
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

	[[nodiscard]] self_type transposed() const
	{
		self_type result(columns_, rows_);
		for (size_type row = 0; row < rows_; ++row)
			for (size_type column = 0; column < columns_; ++column)
				result(column, row) = (*this)(row, column);
		return result;
	}

	[[nodiscard]] self_type transpose() const { return transposed(); }

	[[nodiscard]] T trace() const
	{
		check_square("matrix trace requires a square matrix");
		T result{};
		for (size_type index = 0; index < rows_; ++index)
			result += (*this)(index, index);
		return result;
	}

	[[nodiscard]] T determinant(T tolerance = default_tolerance()) const
		requires std::floating_point<T>
	{
		check_square("matrix determinant requires a square matrix");
		check_tolerance(tolerance);
		if (rows_ == 0)
			return T(1);
		self_type work(*this);
		T result(1);
		for (size_type pivot_column = 0; pivot_column < rows_; ++pivot_column)
		{
			const size_type pivot_row = work.find_pivot(pivot_column, pivot_column);
			const T pivot = work(pivot_row, pivot_column);
			if (absolute(pivot) <= tolerance)
				return T();
			if (pivot_row != pivot_column)
			{
				work.swap_rows(pivot_row, pivot_column);
				result = -result;
			}
			const T diagonal = work(pivot_column, pivot_column);
			result *= diagonal;
			for (size_type row = pivot_column + 1; row < rows_; ++row)
			{
				const T factor = work(row, pivot_column) / diagonal;
				work(row, pivot_column) = T();
				for (size_type column = pivot_column + 1; column < columns_; ++column)
					work(row, column) -= factor * work(pivot_column, column);
			}
		}
		return result;
	}

	[[nodiscard]] self_type inverse(T tolerance = default_tolerance()) const
		requires std::floating_point<T>
	{
		check_square("matrix inverse requires a square matrix");
		check_tolerance(tolerance);
		self_type left(*this);
		self_type right = identity(rows_);
		gauss_jordan(left, right, tolerance);
		return right;
	}

	[[nodiscard]] self_type solve(const self_type& right_hand_side, T tolerance = default_tolerance()) const
		requires std::floating_point<T>
	{
		check_square("matrix solve requires a square coefficient matrix");
		check_tolerance(tolerance);
		if (right_hand_side.rows_ != rows_)
			throw std::invalid_argument("matrix solve right-hand side shape mismatch");
		self_type left(*this);
		self_type result(right_hand_side);
		gauss_jordan(left, result, tolerance);
		return result;
	}

	[[nodiscard]] self_type resolve_ole(self_type right_hand_side) const
		requires std::floating_point<T>
	{
		return solve(right_hand_side);
	}

	[[nodiscard]] self_type minor_matrix(size_type removed_row, size_type removed_column) const
	{
		if (removed_row >= rows_ || removed_column >= columns_)
			throw std::out_of_range("matrix minor coordinate is out of range");
		self_type result(rows_ - 1, columns_ - 1);
		for (size_type source_row = 0, target_row = 0; source_row < rows_; ++source_row)
		{
			if (source_row == removed_row)
				continue;
			for (size_type source_column = 0, target_column = 0; source_column < columns_; ++source_column)
			{
				if (source_column == removed_column)
					continue;
				result(target_row, target_column++) = (*this)(source_row, source_column);
			}
			++target_row;
		}
		return result;
	}

	[[nodiscard]] static self_type cross_prod(const self_type& points)
		requires std::floating_point<T>
	{
		if (points.columns_ != points.rows_ + 1)
			throw std::invalid_argument("cross product requires N-1 row vectors in N dimensions");
		self_type expanded(points.columns_, points.columns_);
		for (size_type row = 0; row < points.rows_; ++row)
			for (size_type column = 0; column < points.columns_; ++column)
				expanded(row + 1, column) = points(row, column);
		self_type result(1, points.columns_);
		for (size_type column = 0; column < points.columns_; ++column)
		{
			const T cofactor = expanded.minor_matrix(0, column).determinant();
			result(0, column) = (column & 1U) == 0 ? cofactor : -cofactor;
		}
		return result;
	}

	[[nodiscard]] bool operator==(const self_type& other) const = default;

	[[nodiscard]] bool operator<(const self_type& other) const
	{
		check_same_shape(other, "matrix comparison shape mismatch");
		return std::ranges::equal(values_, other.values_, std::less<>{});
	}

	[[nodiscard]] bool operator<=(const self_type& other) const
	{
		check_same_shape(other, "matrix comparison shape mismatch");
		return std::ranges::equal(values_, other.values_, std::less_equal<>{});
	}

	[[nodiscard]] self_type ppow(const T& exponent) const
	{
		self_type result(*this);
		result.selfppow(exponent);
		return result;
	}

	self_type& selfppow(const T& exponent)
	{
		for (T& value : values_)
			value = static_cast<T>(std::pow(value, exponent));
		return *this;
	}

	[[nodiscard]] self_type pabs() const
	{
		self_type result(*this);
		return result.selfpabs();
	}

	self_type& selfpabs()
	{
		for (T& value : values_)
			value = absolute(value);
		return *this;
	}

	[[nodiscard]] T psum() const
	{
		T result{};
		for (const T& value : values_)
			result += value;
		return result;
	}

	template<typename Function>
	self_type& selfapply(Function&& function)
	{
		for (T& value : values_)
			std::invoke(function, value);
		return *this;
	}

	template<typename Function>
	[[nodiscard]] self_type apply(Function&& function) const
	{
		self_type result(*this);
		result.selfapply(std::forward<Function>(function));
		return result;
	}

	template<typename Function>
	self_type& selfapply_indexed(Function&& function)
	{
		for (size_type row = 0; row < rows_; ++row)
			for (size_type column = 0; column < columns_; ++column)
				std::invoke(function, (*this)(row, column), row, column);
		return *this;
	}

	template<typename Function>
	[[nodiscard]] self_type apply_indexed(Function&& function) const
	{
		self_type result(*this);
		result.selfapply_indexed(std::forward<Function>(function));
		return result;
	}

	template<typename Function>
	void call(Function&& function) const
	{
		for (const T& value : values_)
			std::invoke(function, value);
	}

	template<typename Function>
	void call_indexed(Function&& function) const
	{
		for (size_type row = 0; row < rows_; ++row)
			for (size_type column = 0; column < columns_; ++column)
				std::invoke(function, (*this)(row, column), row, column);
	}

	[[nodiscard]] T norma(T exponent = T(2)) const
	{
		if (!(exponent > T()))
			throw std::domain_error("matrix norm exponent must be positive");
		long double sum = 0;
		for (const T& value : values_)
			sum += std::pow(static_cast<long double>(absolute(value)), static_cast<long double>(exponent));
		return static_cast<T>(std::pow(sum, 1.0L / static_cast<long double>(exponent)));
	}

	self_type& normalize(T exponent = T(2))
	{
		const T length = norma(exponent);
		if (length == T())
			throw std::domain_error("cannot normalize a zero matrix");
		return *this /= length;
	}

	[[nodiscard]] std::pair<T, T> minmax() const
	{
		bool found = false;
		T minimum{};
		T maximum{};
		for (const T& value : values_)
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
		if (!found)
			throw std::domain_error("matrix has no finite values");
		return {minimum, maximum};
	}

	[[nodiscard]] static constexpr T default_tolerance() noexcept
	{
		if constexpr (std::floating_point<T>)
			return std::numeric_limits<T>::epsilon() * T(64);
		else
			return T();
	}

private:
	size_type rows_ = 0;
	size_type columns_ = 0;
	std::vector<T> values_;

	[[nodiscard]] static size_type checked_area(size_type rows, size_type columns)
	{
		if (rows != 0 && columns > (std::numeric_limits<size_type>::max)() / rows)
			throw std::length_error("matrix dimensions overflow");
		return rows * columns;
	}

	template<typename Iterator>
	void assign_rows(Iterator first, Iterator last)
	{
		rows_ = static_cast<size_type>(std::distance(first, last));
		columns_ = rows_ == 0 ? 0 : first->size();
		values_.reserve(checked_area(rows_, columns_));
		for (Iterator current = first; current != last; ++current)
		{
			if (current->size() != columns_)
				throw std::invalid_argument("matrix initializer rows must have equal lengths");
			values_.insert(values_.end(), current->begin(), current->end());
		}
	}

	void check_coordinate(size_type row, size_type column) const
	{
		if (row >= rows_ || column >= columns_)
			throw std::out_of_range("matrix coordinate is out of range");
	}

	void check_same_shape(const self_type& other, const char* message) const
	{
		if (rows_ != other.rows_ || columns_ != other.columns_)
			throw std::invalid_argument(message);
	}

	void check_square(const char* message) const
	{
		if (rows_ != columns_)
			throw std::invalid_argument(message);
	}

	[[nodiscard]] static T absolute(T value)
	{
		if constexpr (std::is_unsigned_v<T>)
			return value;
		else if constexpr (std::integral<T>)
		{
			if (value == (std::numeric_limits<T>::lowest)())
				throw std::overflow_error("matrix absolute value is not representable");
			return value < T() ? -value : value;
		}
		else
		{
			using std::abs;
			return abs(value);
		}
	}

	static void check_tolerance(T tolerance)
		requires std::floating_point<T>
	{
		if (!std::isfinite(tolerance) || tolerance < T())
			throw std::invalid_argument("matrix tolerance must be finite and nonnegative");
	}

	void swap_rows(size_type first, size_type second)
	{
		if (first == second)
			return;
		for (size_type column = 0; column < columns_; ++column)
			std::swap((*this)(first, column), (*this)(second, column));
	}

	[[nodiscard]] size_type find_pivot(size_type first_row, size_type column) const
	{
		size_type pivot = first_row;
		for (size_type row = first_row + 1; row < rows_; ++row)
		{
			if (absolute((*this)(pivot, column)) < absolute((*this)(row, column)))
				pivot = row;
		}
		return pivot;
	}

	static void gauss_jordan(self_type& left, self_type& right, T tolerance)
		requires std::floating_point<T>
	{
		for (size_type pivot_column = 0; pivot_column < left.rows_; ++pivot_column)
		{
			const size_type pivot_row = left.find_pivot(pivot_column, pivot_column);
			if (absolute(left(pivot_row, pivot_column)) <= tolerance)
				throw std::domain_error("matrix is singular");
			left.swap_rows(pivot_row, pivot_column);
			right.swap_rows(pivot_row, pivot_column);
			const T pivot = left(pivot_column, pivot_column);
			for (size_type column = 0; column < left.columns_; ++column)
				left(pivot_column, column) /= pivot;
			for (size_type column = 0; column < right.columns_; ++column)
				right(pivot_column, column) /= pivot;

			for (size_type row = 0; row < left.rows_; ++row)
			{
				if (row == pivot_column)
					continue;
				const T factor = left(row, pivot_column);
				left(row, pivot_column) = T();
				for (size_type column = pivot_column + 1; column < left.columns_; ++column)
					left(row, column) -= factor * left(pivot_column, column);
				for (size_type column = 0; column < right.columns_; ++column)
					right(row, column) -= factor * right(pivot_column, column);
			}
		}
	}

	template<typename U>
	friend std::ostream& operator<<(std::ostream&, const matrix<U>&);
	template<typename U>
	friend std::istream& operator>>(std::istream&, matrix<U>&);
};

template<typename T>
[[nodiscard]] matrix<T> operator*(const T& scalar, const matrix<T>& value)
{
	return value * scalar;
}

template<typename T>
std::ostream& operator<<(std::ostream& output, const matrix<T>& value)
{
	const std::streamsize original_precision = output.precision();
	if constexpr (std::floating_point<T>)
		output << std::setprecision(std::numeric_limits<T>::max_digits10);
	output << value.rows_ << ' ' << value.columns_ << '\n';
	for (std::size_t row = 0; row < value.rows_; ++row)
	{
		for (std::size_t column = 0; column < value.columns_; ++column)
		{
			if (column != 0)
				output << ' ';
			output << value(row, column);
		}
		output << '\n';
	}
	output.precision(original_precision);
	return output;
}

template<typename T>
std::istream& operator>>(std::istream& input, matrix<T>& value)
{
	std::size_t rows;
	std::size_t columns;
	if (!(input >> rows >> columns))
		return input;
	matrix<T> parsed(rows, columns);
	for (T& element : parsed.values_)
	{
		if (!(input >> element))
			return input;
	}
	value.swap(parsed);
	return input;
}

template<typename T>
void swap(matrix<T>& left, matrix<T>& right) noexcept
{
	left.swap(right);
}

} // namespace dixelu
