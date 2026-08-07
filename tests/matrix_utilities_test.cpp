#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "matrix.h"
#include "sq_matrix.h"

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

bool approximately_equal(double left, double right, double tolerance = 1e-9)
{
	const double scale = (std::max)({1.0, std::abs(left), std::abs(right)});
	return std::abs(left - right) <= tolerance * scale;
}

struct reference_matrix
{
	std::size_t rows = 0;
	std::size_t columns = 0;
	std::vector<double> values;

	reference_matrix() = default;
	reference_matrix(std::size_t row_count, std::size_t column_count)
		: rows(row_count), columns(column_count), values(row_count * column_count)
	{
	}

	[[nodiscard]] double& operator()(std::size_t row, std::size_t column)
	{
		return values[row * columns + column];
	}

	[[nodiscard]] double operator()(std::size_t row, std::size_t column) const
	{
		return values[row * columns + column];
	}
};

reference_matrix reference_add(const reference_matrix& left, const reference_matrix& right, double sign = 1.0)
{
	reference_matrix result(left.rows, left.columns);
	for (std::size_t index = 0; index < result.values.size(); ++index)
		result.values[index] = left.values[index] + sign * right.values[index];
	return result;
}

reference_matrix reference_multiply(const reference_matrix& left, const reference_matrix& right)
{
	reference_matrix result(left.rows, right.columns);
	for (std::size_t row = 0; row < left.rows; ++row)
		for (std::size_t column = 0; column < right.columns; ++column)
			for (std::size_t inner = 0; inner < left.columns; ++inner)
				result(row, column) += left(row, inner) * right(inner, column);
	return result;
}

reference_matrix reference_transpose(const reference_matrix& value)
{
	reference_matrix result(value.columns, value.rows);
	for (std::size_t row = 0; row < value.rows; ++row)
		for (std::size_t column = 0; column < value.columns; ++column)
			result(column, row) = value(row, column);
	return result;
}

reference_matrix reference_identity(std::size_t size)
{
	reference_matrix result(size, size);
	for (std::size_t index = 0; index < size; ++index)
		result(index, index) = 1.0;
	return result;
}

reference_matrix reference_power(const reference_matrix& value, std::size_t exponent)
{
	reference_matrix result = reference_identity(value.rows);
	for (std::size_t count = 0; count < exponent; ++count)
		result = reference_multiply(result, value);
	return result;
}

double reference_determinant(const reference_matrix& value)
{
	if (value.rows == 0)
		return 1.0;
	if (value.rows == 1)
		return value(0, 0);
	double result = 0;
	for (std::size_t removed_column = 0; removed_column < value.columns; ++removed_column)
	{
		reference_matrix minor(value.rows - 1, value.columns - 1);
		for (std::size_t source_row = 1; source_row < value.rows; ++source_row)
		{
			for (std::size_t source_column = 0, target_column = 0; source_column < value.columns; ++source_column)
			{
				if (source_column == removed_column)
					continue;
				minor(source_row - 1, target_column++) = value(source_row, source_column);
			}
		}
		const double cofactor = value(0, removed_column) * reference_determinant(minor);
		result += (removed_column & 1U) == 0 ? cofactor : -cofactor;
	}
	return result;
}

dixelu::matrix<double> to_dynamic(const reference_matrix& source)
{
	dixelu::matrix<double> result(source.rows, source.columns);
	for (std::size_t row = 0; row < source.rows; ++row)
		for (std::size_t column = 0; column < source.columns; ++column)
			result(row, column) = source(row, column);
	return result;
}

void require_equal(const dixelu::matrix<double>& actual, const reference_matrix& expected, const char* message)
{
	require(actual.rows() == expected.rows && actual.cols() == expected.columns, message);
	for (std::size_t row = 0; row < expected.rows; ++row)
		for (std::size_t column = 0; column < expected.columns; ++column)
			require(approximately_equal(actual(row, column), expected(row, column)), message);
}

reference_matrix random_reference(
	std::mt19937_64& random,
	std::size_t rows,
	std::size_t columns,
	std::int64_t magnitude = 7)
{
	reference_matrix result(rows, columns);
	const std::uint64_t width = static_cast<std::uint64_t>(magnitude * 2 + 1);
	for (double& value : result.values)
		value = static_cast<double>(static_cast<std::int64_t>(random() % width) - magnitude);
	return result;
}

constexpr bool fixed_compile_time_regressions()
{
	dixelu::sq_matrix<int, 2> value{{{1, 2}, {3, 4}}};
	const auto transposed = value.transposed();
	if (transposed(0, 1) != 3 || transposed(1, 0) != 2)
		return false;
	value.transpose();
	if (value != transposed)
		return false;
	const auto squared = dixelu::sq_matrix<int, 2>{{{1, 1}, {1, 0}}} ^ 2;
	return squared(0, 0) == 2 && squared(0, 1) == 1 &&
		squared(1, 0) == 1 && squared(1, 1) == 1;
}

static_assert(fixed_compile_time_regressions());

void test_dynamic_contracts()
{
	using matrix_type = dixelu::matrix<double>;

	const matrix_type empty;
	require(empty.rows() == 0 && empty.cols() == 0 && empty.empty(), "default matrix must be empty");
	require(matrix_type::identity(0).determinant() == 1.0, "empty determinant must be one");

	matrix_type rectangular{{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}}};
	require(rectangular.at(1, 2) == 6.0, "matrix coordinates must be row then column");
	require(rectangular[0][1] == 2.0, "matrix row view mismatch");
	require(throws<std::out_of_range>([&] { (void)rectangular.at(2, 0); }), "matrix at must bounds-check rows");
	require(
		throws<std::invalid_argument>([] { (void)matrix_type{{{1.0, 2.0}, {3.0}}}; }),
		"matrix must reject ragged initializers");

	require(matrix_type(2, 3) != matrix_type(3, 2), "matrix equality must compare both dimensions");
	require(matrix_type(2, 3) != matrix_type(2, 4), "matrix equality must compare column count");
	require(
		throws<std::invalid_argument>([] { (void)(matrix_type(2, 3) + matrix_type(3, 2)); }),
		"matrix addition must reject shape mismatch");
	require(
		throws<std::invalid_argument>([] { (void)(matrix_type(2, 3) < matrix_type(3, 3)); }),
		"matrix ordering must reject either dimension mismatch");
	require(
		throws<std::invalid_argument>([] { (void)(matrix_type(2, 3) * matrix_type(2, 3)); }),
		"matrix multiplication must reject incompatible dimensions");
	require(
		throws<std::invalid_argument>([&] { (void)rectangular.trace(); }),
		"matrix trace must require a square matrix");
	require(
		throws<std::invalid_argument>([&] { (void)(rectangular ^ 2); }),
		"matrix power must require a square matrix");
	require(
		throws<std::domain_error>([&] { (void)(rectangular / 0.0); }),
		"matrix scalar division by zero must throw");

	const matrix_type negative{{{-5.0, -2.0}, {-9.0, -4.0}}};
	require(negative.minmax() == std::pair{-9.0, -2.0}, "matrix minmax must handle all-negative values");
	require(
		dixelu::matrix<int>{{{-5, -2}, {-9, -4}}}.minmax() == std::pair{-9, -2},
		"integer matrix minmax mismatch");
	const matrix_type partly_nonfinite{{{
		std::numeric_limits<double>::quiet_NaN(), -3.0,
		std::numeric_limits<double>::infinity(), 4.0}}};
	require(partly_nonfinite.minmax() == std::pair{-3.0, 4.0}, "matrix minmax must ignore non-finite values");
	const matrix_type nonfinite{{{std::numeric_limits<double>::quiet_NaN()}}};
	require(throws<std::domain_error>([&] { (void)nonfinite.minmax(); }), "matrix minmax must reject no finite values");

	const matrix_type pivoting{{{0.0, 1.0}, {1.0, 0.0}}};
	require(pivoting.determinant() == -1.0, "matrix determinant must account for row-swap sign");
	require(pivoting.inverse() == pivoting, "matrix inverse mismatch");
	const matrix_type singular{{{1.0, 2.0}, {2.0, 4.0}}};
	require(throws<std::domain_error>([&] { (void)singular.inverse(); }), "singular matrix inverse must throw");
	require(
		throws<std::invalid_argument>([&] { (void)pivoting.inverse(-1.0); }),
		"matrix inverse must reject negative tolerance");
	require(
		(matrix_type::identity(2) ^ (std::numeric_limits<std::int64_t>::min)()) == matrix_type::identity(2),
		"matrix power must handle the minimum signed exponent");

	const matrix_type rows{{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}}};
	require(matrix_type::cross_prod(rows) == matrix_type{{{0.0, 0.0, 1.0}}}, "dynamic cross product mismatch");

	std::ostringstream output;
	output << rectangular;
	matrix_type round_trip;
	std::istringstream input(output.str());
	input >> round_trip;
	require(input.good() || input.eof(), "matrix stream extraction must succeed");
	require(round_trip == rectangular, "matrix stream round trip mismatch");
	matrix_type preserved{{{7.0}}};
	std::istringstream invalid("2 2 1 2 broken");
	invalid >> preserved;
	require(invalid.fail() && preserved == matrix_type{{{7.0}}}, "failed matrix input must preserve its target");
}

void test_dynamic_randomized()
{
	std::mt19937_64 random(0xD1A61CA1ULL);
	for (std::size_t iteration = 0; iteration < 2'000; ++iteration)
	{
		const std::size_t rows = 1 + random() % 5;
		const std::size_t inner = 1 + random() % 5;
		const std::size_t columns = 1 + random() % 5;
		const reference_matrix left_reference = random_reference(random, rows, inner);
		const reference_matrix same_shape_reference = random_reference(random, rows, inner);
		const reference_matrix right_reference = random_reference(random, inner, columns);
		const auto left = to_dynamic(left_reference);
		const auto same_shape = to_dynamic(same_shape_reference);
		const auto right = to_dynamic(right_reference);

		require_equal(left + same_shape, reference_add(left_reference, same_shape_reference), "random matrix addition mismatch");
		require_equal(left - same_shape, reference_add(left_reference, same_shape_reference, -1.0), "random matrix subtraction mismatch");
		require_equal(left * right, reference_multiply(left_reference, right_reference), "random matrix multiplication mismatch");
		require_equal(left.transposed(), reference_transpose(left_reference), "random matrix transpose mismatch");
	}

	for (std::size_t iteration = 0; iteration < 750; ++iteration)
	{
		const std::size_t size = 1 + random() % 4;
		const std::size_t exponent = random() % 7;
		const reference_matrix reference = random_reference(random, size, size, 3);
		const auto value = to_dynamic(reference);
		require_equal(value ^ static_cast<std::int64_t>(exponent), reference_power(reference, exponent), "random matrix power mismatch");
		require(
			approximately_equal(value.determinant(), reference_determinant(reference)),
			"random matrix determinant mismatch");
	}

	for (std::size_t iteration = 0; iteration < 500; ++iteration)
	{
		const std::size_t size = 1 + random() % 4;
		reference_matrix reference = random_reference(random, size, size, 3);
		for (std::size_t index = 0; index < size; ++index)
			reference(index, index) += static_cast<double>(size * 5);
		const auto value = to_dynamic(reference);
		const auto inverse = value.inverse();
		require_equal(value * inverse, reference_identity(size), "random matrix inverse mismatch");

		const reference_matrix rhs_reference = random_reference(random, size, 2);
		const auto rhs = to_dynamic(rhs_reference);
		require_equal(value * value.solve(rhs), rhs_reference, "random matrix solve mismatch");
		require((value ^ -1) == inverse, "negative matrix power mismatch");
	}
}

template<std::size_t Dimensions>
dixelu::sq_matrix<double, Dimensions> to_fixed(const reference_matrix& source)
{
	dixelu::sq_matrix<double, Dimensions> result;
	for (std::size_t row = 0; row < Dimensions; ++row)
		for (std::size_t column = 0; column < Dimensions; ++column)
			result(row, column) = source(row, column);
	return result;
}

template<std::size_t Dimensions>
void require_equal(
	const dixelu::sq_matrix<double, Dimensions>& actual,
	const reference_matrix& expected,
	const char* message)
{
	for (std::size_t row = 0; row < Dimensions; ++row)
		for (std::size_t column = 0; column < Dimensions; ++column)
			require(approximately_equal(actual(row, column), expected(row, column)), message);
}

template<std::size_t Dimensions>
void test_fixed_randomized_dimension(std::mt19937_64& random, std::size_t iterations)
{
	for (std::size_t iteration = 0; iteration < iterations; ++iteration)
	{
		const reference_matrix left_reference = random_reference(random, Dimensions, Dimensions, 3);
		const reference_matrix right_reference = random_reference(random, Dimensions, Dimensions, 3);
		const auto left = to_fixed<Dimensions>(left_reference);
		const auto right = to_fixed<Dimensions>(right_reference);
		const std::size_t exponent = random() % 6;
		require_equal(left + right, reference_add(left_reference, right_reference), "random fixed-matrix addition mismatch");
		require_equal(left - right, reference_add(left_reference, right_reference, -1.0), "random fixed-matrix subtraction mismatch");
		require_equal(left * right, reference_multiply(left_reference, right_reference), "random fixed-matrix multiplication mismatch");
		require_equal(left.transposed(), reference_transpose(left_reference), "random fixed-matrix transpose mismatch");
		require_equal(left ^ static_cast<std::int64_t>(exponent), reference_power(left_reference, exponent), "random fixed-matrix power mismatch");
		require(approximately_equal(left.determinant(), reference_determinant(left_reference)), "random fixed determinant mismatch");
	}
}

void test_fixed_contracts_and_randomized()
{
	using fixed = dixelu::sq_matrix<double, 3>;
	using vector = dixelu::point<double, 3>;
	const fixed original{{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}, {7.0, 8.0, 10.0}}};
	fixed mutating = original;
	mutating.transpose();
	require(mutating == original.transposed(), "fixed transpose must swap each pair exactly once");
	require(mutating.transposed() == original, "fixed double transpose mismatch");
	require(original.at(2, 1) == 8.0, "fixed coordinates must be row then column");
	require(throws<std::out_of_range>([&] { (void)original.at(3, 0); }), "fixed at must bounds-check");
	require(original.ppow(2.0)(0, 2) == 9.0, "fixed elementwise power must use its exponent");
	require(original.minmax() == std::pair{1.0, 10.0}, "fixed minmax mismatch");
	require(
		(fixed::identity() ^ (std::numeric_limits<std::int64_t>::min)()) == fixed::identity(),
		"fixed power must handle the minimum signed exponent");

	const auto block = original.to<2>(1);
	require(block(0, 0) == 5.0 && block(0, 1) == 6.0 && block(1, 0) == 8.0 && block(1, 1) == 10.0,
		"fixed block conversion mismatch");
	const auto minor = original.minor_matrix(0, 1);
	require(minor(0, 0) == 4.0 && minor(0, 1) == 6.0 && minor(1, 0) == 7.0 && minor(1, 1) == 10.0,
		"fixed minor mismatch");

	const vector point{1.0, -2.0, 3.0};
	require(-point == vector{-1.0, 2.0, -3.0}, "point unary minus mismatch");
	require(point == vector{1.0, -2.0, 3.0}, "point unary minus must not mutate its operand");
	require(original * vector{1.0, 0.0, 0.0} == vector{1.0, 4.0, 7.0}, "fixed matrix-vector product mismatch");

	const std::array<vector, 2> basis{vector{1.0, 0.0, 0.0}, vector{0.0, 1.0, 0.0}};
	require(dixelu::cross_prod(basis) == vector{0.0, 0.0, 1.0}, "fixed cross product mismatch");
	const fixed singular{{{1.0, 2.0, 3.0}, {2.0, 4.0, 6.0}, {0.0, 1.0, 1.0}}};
	require(throws<std::domain_error>([&] { (void)singular.inverse(); }), "singular fixed inverse must throw");

	std::mt19937_64 random(0xF17ED123ULL);
	test_fixed_randomized_dimension<1>(random, 300);
	test_fixed_randomized_dimension<2>(random, 500);
	test_fixed_randomized_dimension<3>(random, 750);
	test_fixed_randomized_dimension<4>(random, 450);

	for (std::size_t iteration = 0; iteration < 500; ++iteration)
	{
		reference_matrix reference = random_reference(random, 3, 3, 3);
		for (std::size_t index = 0; index < 3; ++index)
			reference(index, index) += 15.0;
		const fixed value = to_fixed<3>(reference);
		const fixed inverse = value.inverse();
		require_equal(value * inverse, reference_identity(3), "random fixed inverse mismatch");
		const vector expected_solution{
			static_cast<double>(static_cast<int>(random() % 11) - 5),
			static_cast<double>(static_cast<int>(random() % 11) - 5),
			static_cast<double>(static_cast<int>(random() % 11) - 5)};
		const vector right_hand_side = value * expected_solution;
		const vector actual_solution = value.solve(right_hand_side);
		for (std::size_t index = 0; index < 3; ++index)
			require(approximately_equal(actual_solution[index], expected_solution[index]), "random fixed solve mismatch");
		require((value ^ -1) == inverse, "negative fixed power mismatch");
	}
}

} // namespace

int main()
{
	test_dynamic_contracts();
	test_dynamic_randomized();
	test_fixed_contracts_and_randomized();
}
