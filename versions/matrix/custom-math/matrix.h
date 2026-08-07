#pragma once
#ifndef _MATRIX_H_
#define _MATRIX_H_

#include <functional>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <utility>
#include <vector>
#include <string>
#include <cmath>

namespace dixelu {

namespace {


}

template<typename general_float_type>
class matrix {
public:
	using line = std::vector<general_float_type>;
	using self_type = matrix<general_float_type>;
	static constexpr general_float_type EPSILON = std::numeric_limits<general_float_type>::epsilon();

	matrix();
	matrix(size_t rows, size_t cols);
	matrix(size_t size);
	matrix(const matrix& rightMatrix);
	matrix(const std::initializer_list<std::vector<general_float_type>>& list);
	matrix(const std::vector<line>& list);
	static self_type E_matrix(size_t size);
	static self_type Diagonal(const line& diag_values);
	inline ~matrix() {};

	inline std::pair<size_t, size_t> size() const;
	inline size_t rows() const;
	inline size_t cols() const;
	inline void resize(size_t newRows, size_t newCols);
	inline void swap(matrix& rightMatrix);

	inline general_float_type& at(size_t x, size_t y);
	inline const general_float_type& at(size_t x, size_t y) const;
	inline line& operator[](size_t rows);
	inline const line& operator[](size_t rows) const;

	inline self_type get_row(size_t row) const;
	inline self_type get_col(size_t col) const;
	inline self_type& set_row(size_t row, const self_type& mx_row);
	inline self_type& set_col(size_t col, const self_type& mx_col);

	inline self_type& operator=(const self_type& rightMatrix);
	inline self_type operator*(general_float_type a) const;
	inline self_type& operator*=(general_float_type a);
	inline self_type operator+(const self_type& rightMatrix) const;
	inline self_type operator-(const self_type& rightMatrix) const;
	inline self_type& operator+=(const self_type& rightMatrix);
	inline self_type& operator-=(const self_type& rightMatrix);
	inline self_type operator/(general_float_type a) const;
	inline self_type& operator/=(general_float_type a);
	inline self_type operator*(const self_type& rightMatrix) const;
	inline self_type operator^(int64_t degree) const;
	inline bool operator==(const self_type& rightMatrix) const;

	inline self_type ppow(general_float_type p) const;
	inline self_type& selfppow(general_float_type p);
	inline self_type apply(const std::function<void(general_float_type&)>& func) const;
	inline self_type& selfapply(const std::function<void(general_float_type&)>& func);
	inline self_type apply_indexed(const std::function<void(general_float_type&, size_t, size_t)>& func) const;
	inline self_type& selfapply_indexed(const std::function<void(general_float_type&, size_t, size_t)>& func);
	inline void call_indexed(const std::function<void(general_float_type, size_t, size_t)>& func) const;
	inline void call(const std::function<void(general_float_type)>& func) const;
	inline general_float_type psum() const;
	inline self_type pabs() const;
	inline self_type& selfpabs();
	inline self_type& normalize(general_float_type p = 2);
	inline general_float_type norma(general_float_type p = 2) const;

	inline self_type transpose() const;
	inline general_float_type trace() const;
	inline self_type inverse() const;
	inline general_float_type determinant() const;
	inline self_type resolve_ole(self_type point) const;

	template<typename gft>	
	friend inline std::ostream& operator<<(std::ostream& out, const matrix<gft>& rightMatrix);
	
	template<typename gft>	
	friend inline std::istream& operator>>(std::istream& in, matrix<gft>& rightMatrix);
	
	template<typename gft>	
	friend inline matrix operator*(general_float_type a, const matrix<gft>& rightMatrix);

	inline matrix minor_matrix(const size_t& x_minor, const size_t& y_minor) const;

	inline static matrix cross_prod(const self_type& points_in_matrix);

	inline bool operator<(const self_type& comparation_m) const;
	inline bool operator<=(const self_type& comparation_m) const;

	inline std::pair<general_float_type, general_float_type> minmax() const;
private:
	std::vector<line> _matrix;
	general_float_type utilization = 0;
};

template<typename general_float_type>
matrix<general_float_type>::matrix() : _matrix(1, line(1, 0.)) {}

template<typename general_float_type>
matrix<general_float_type>::matrix(size_t rows, size_t cols) {
	_matrix.assign(rows, line(cols, 0.));
}

template<typename general_float_type>
matrix<general_float_type>::matrix(size_t size) {
	_matrix.assign(size, line(size, 0.));
}

template<typename general_float_type>
matrix<general_float_type>::matrix(const matrix& rightMatrix) {
	_matrix = rightMatrix._matrix;
}

template<typename general_float_type>
inline matrix<general_float_type>::matrix(const std::initializer_list<std::vector<general_float_type>>& list) {
	for (auto&& line : list) {
		_matrix.push_back(line);
	}
}

template<typename general_float_type>
matrix<general_float_type>::matrix(const std::vector<line>& list) {
	for (auto&& line : list) {
		_matrix.push_back(line);
	}
}

template<typename general_float_type>
matrix<general_float_type> matrix<general_float_type>::E_matrix(size_t size) {
	self_type unitMatrix(size);
	for (int i = 0; i < size; i++)
		unitMatrix._matrix[i][i] = 1.;
	return unitMatrix;
}

template<typename general_float_type>
matrix<general_float_type> matrix<general_float_type>::Diagonal(const line& diagValues) {
	self_type diagMatrix(diagValues.size());
	for (int i = 0; i < diagValues.size(); i++)
		diagMatrix._matrix[i][i] = diagValues[i];
	return diagMatrix;
}

template<typename general_float_type>
inline size_t matrix<general_float_type>::rows() const {
	return _matrix.size();
}

template<typename general_float_type>
inline size_t matrix<general_float_type>::cols() const {
	if (_matrix.size())
		return _matrix.front().size();
	return 0;
}

template<typename general_float_type>
inline std::pair<size_t, size_t> matrix<general_float_type>::size() const {
	return { rows(),cols() };
}

template<typename general_float_type>
inline void matrix<general_float_type>::resize(size_t newRows, size_t newCols) {
	for (auto& l : _matrix) {
		l.resize(newCols, 0);
	}
	_matrix.resize(newRows, line(newCols, 0));
}

template<typename general_float_type>
inline void matrix<general_float_type>::swap(self_type& rightMatrix) {
	_matrix.swap(rightMatrix._matrix);
}

template<typename general_float_type>
inline general_float_type& matrix<general_float_type>::at(size_t x, size_t y) {
	if (x < cols() && y < rows()) {
		return _matrix[y][x];
	}
	else
		return utilization;
}

template<typename general_float_type>
inline const general_float_type& matrix<general_float_type>::at(size_t x, size_t y) const {
	if (x < cols() && y < rows()) {
		return _matrix[y][x];
	}
	else
		return utilization;
}

template<typename general_float_type>
inline typename matrix<general_float_type>::line& matrix<general_float_type>::operator[](size_t rows) {
	return _matrix[rows];
}

template<typename general_float_type>
inline const typename matrix<general_float_type>::line& matrix<general_float_type>::operator[](size_t rows) const {
	return _matrix[rows];
}

template<typename general_float_type>
inline matrix<general_float_type>& matrix<general_float_type>::operator=(const self_type& rightMatrix) {
	_matrix = rightMatrix._matrix;
	return *this;
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::operator*(general_float_type Num) const {
	self_type prodMatrix(*this);
	for (auto&& l : prodMatrix._matrix) {
		for (auto&& d : l) {
			d *= Num;
		}
	}
	return prodMatrix;
}

template<typename general_float_type>
inline matrix<general_float_type>& matrix<general_float_type>::operator*=(general_float_type Num) {
	for (auto&& l : _matrix) {
		for (auto&& d : l) {
			d *= Num;
		}
	}
	return *this;
}

template<typename general_float_type>
inline matrix<general_float_type>& matrix<general_float_type>::operator/=(general_float_type Num) {
	for (auto&& l : _matrix) {
		for (auto&& d : l) {
			d /= Num;
		}
	}
	return *this;
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::operator+(const self_type& rightMatrix) const {
	self_type sumMatrix(*this);
	for (size_t y = 0; y < rows(); y++) {
		for (size_t x = 0; x < cols(); x++) {
			sumMatrix.at(x, y) += rightMatrix.at(x, y);
		}
	}
	return sumMatrix;
}

template<typename general_float_type>
inline matrix<general_float_type>& matrix<general_float_type>::operator+=(const self_type& rightMatrix) {
	for (size_t y = 0; y < rows(); y++) {
		for (size_t x = 0; x < cols(); x++) {
			at(x, y) += rightMatrix.at(x, y);
		}
	}
	return *this;
}

template<typename general_float_type>
inline matrix<general_float_type>& matrix<general_float_type>::operator-=(const self_type& rightMatrix) {
	for (size_t y = 0; y < rows(); y++) {
		for (size_t x = 0; x < cols(); x++) {
			at(x, y) -= rightMatrix.at(x, y);
		}
	}
	return *this;
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::operator-(const self_type& rightMatrix) const {
	self_type diffMatrix(*this);
	for (size_t y = 0; y < rows(); y++) {
		for (size_t x = 0; x < cols(); x++) {
			diffMatrix.at(x, y) -= rightMatrix.at(x, y);
		}
	}
	return diffMatrix;
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::operator/(general_float_type Number) const {
	return (*this * (1. / Number));
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::operator*(const self_type& rightMatrix) const {
	if (cols() != rightMatrix.rows())
		return matrix();
	self_type prodMatrix(rows(), rightMatrix.cols());
	for (size_t y = 0; y < prodMatrix.rows(); y++) {
		for (size_t x = 0; x < prodMatrix.cols(); x++) {
			for (size_t i = 0; i < cols(); i++) {
				prodMatrix[y][x] += _matrix[y][i] * rightMatrix[i][x];
			}
		}
	}
	return prodMatrix;
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::operator^(int64_t degree) const {
	if (rows() != cols())
		return self_type();
	bool inverse = false;
	if (degree < 0) {
		inverse = true;
		degree = -degree;
		//std::cout << a;
	}
	else if (!degree)
		return self_type::E_matrix(rows());
	self_type curMatrix = self_type::E_matrix(rows()), degCoMatrix = *this;
	while (degree) {
		switch (degree & 1) {
		case 1:
			curMatrix = curMatrix * degCoMatrix;
			degCoMatrix = degCoMatrix * degCoMatrix;
			break;
		case 0:
			degCoMatrix = degCoMatrix * degCoMatrix;
			break;
		}
		degree >>= 1;
	}
	return inverse ? curMatrix.inverse() : curMatrix;
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::transpose() const {
	self_type transposedMatix(cols(), rows());
	for (size_t y = 0; y < rows(); y++) {
		for (size_t x = 0; x < cols(); x++) {
			transposedMatix[x][y] = _matrix[y][x];
		}
	}
	return transposedMatix;
}

template<typename general_float_type>
inline general_float_type matrix<general_float_type>::trace() const {
	general_float_type sum = 0;
	for (size_t i = 0; i < rows(); i++) {
		sum += at(i, i);
	}
	return sum;
}

template<typename general_float_type>
inline general_float_type matrix<general_float_type>::determinant() const {//gauss
	if (rows() != cols())
		return 0;
	general_float_type multiplier = 1, maxValue = 0;
	self_type thisMatrix(*this);
	size_t maxID = 0;
	for (size_t curColumn = 0; curColumn < rows(); curColumn++) {
		maxID = curColumn;
		maxValue = 0;
		for (size_t curRow = maxID; curRow < rows(); curRow++) {
			if ((std::abs)(thisMatrix[curRow][curColumn]) > maxValue) {
				maxValue = (std::abs)(thisMatrix[curRow][curColumn]);
				maxID = curRow;
			}
		}
		if ((std::abs)(thisMatrix[maxID][curColumn]) >= EPSILON)
			thisMatrix[maxID].swap(thisMatrix[curColumn]);
		else
			return 0;
		for (size_t curRow = 0; curRow < rows(); curRow++) {
			if (curRow == curColumn)
				continue;
			general_float_type rowMultiplier = -1 * (thisMatrix[curRow][curColumn] / thisMatrix[curColumn][curColumn]);
			for (size_t sumColumn = curColumn; sumColumn < cols(); sumColumn++) {
				thisMatrix[curRow][sumColumn] += rowMultiplier * thisMatrix[curColumn][sumColumn];
			}
		}
	}
	for (size_t i = 0; i < rows(); i++)
		multiplier *= thisMatrix[i][i];
	return multiplier;
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::inverse() const {
	if (rows() != cols())
		return self_type();
	self_type thisMatrix(*this);
	self_type finalMatrix;
	finalMatrix.resize(rows(), cols());
	general_float_type maxValue = 0;
	thisMatrix.resize(rows(), cols() * 2);
	for (size_t i = 0; i < cols(); i++)
		thisMatrix[i][i + cols()] = 1;
	size_t maxID = 0;
	for (size_t curColumn = 0; curColumn < rows(); curColumn++) {
		maxValue = 0;
		maxID = curColumn;
		for (size_t curRow = maxID; curRow < rows(); curRow++) {
			if ((std::abs)(thisMatrix[curRow][curColumn]) > maxValue) {
				maxValue = (std::abs)(thisMatrix[curRow][curColumn]);
				maxID = curRow;
			}
		}
		if ((std::abs)(thisMatrix[maxID][curColumn]) >= EPSILON)
			thisMatrix[maxID].swap(thisMatrix[curColumn]);
		else
			return matrix(rows(), rows());
		for (size_t curRow = 0; curRow < rows(); curRow++) {
			if (curRow == curColumn || (std::abs)(thisMatrix[curRow][curColumn]) < EPSILON)
				continue;
			//std::cout << M << '\n';
			general_float_type rowMultiplier = -1 * (thisMatrix[curRow][curColumn] / thisMatrix[curColumn][curColumn]);
			for (size_t sumColumn = curColumn; sumColumn < thisMatrix.cols(); sumColumn++) {
				thisMatrix[curRow][sumColumn] += rowMultiplier * thisMatrix[curColumn][sumColumn];
			}
		}
	}
	for (size_t i = 0; i < rows(); i++) {
		general_float_type multiplier = 1 / thisMatrix[i][i];
		for (size_t col = i; col < 2 * rows(); col++) {
			thisMatrix[i][col] *= multiplier;
		}
	}
	for (size_t y = 0; y < rows(); y++) {
		for (size_t x = 0; x < cols(); x++) {
			if ((std::abs)(thisMatrix[y][x + rows()]) < EPSILON)
				thisMatrix[y][x + rows()] = (std::abs)(thisMatrix[y][x + rows()]);
			finalMatrix[y][x] = thisMatrix[y][x + rows()];
		}
	}
	return finalMatrix;
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::resolve_ole(self_type point) const {
	general_float_type multiplier = 1, maxValue = 0;
	self_type thisMatrix(*this);
	self_type answer(cols(), 1);
	size_t maxID = 0;
	for (size_t curColumn = 0; curColumn < (std::min)(cols(), rows()); curColumn++) {
		maxID = curColumn;
		maxValue = 0;
		for (size_t curRow = maxID; curRow < rows(); curRow++) {
			if ((std::abs)(thisMatrix[curRow][curColumn]) > maxValue) {
				maxValue = (std::abs)(thisMatrix[curRow][curColumn]);
				maxID = curRow;
			}
		}
		if ((std::abs)(thisMatrix[maxID][curColumn]) >= EPSILON) {
			point[maxID].swap(point[curColumn]);
			thisMatrix[maxID].swap(thisMatrix[curColumn]);
		}
		else { // multiple solutions
			continue;
		}
		for (size_t curRow = 0; curRow < rows(); curRow++) {
			if (curRow == curColumn)
				continue;
			general_float_type rowMultiplier = -1 * (thisMatrix[curRow][curColumn] / thisMatrix[curColumn][curColumn]);
			point.at(0, curRow) += rowMultiplier * point.at(0, curColumn);
			for (size_t sumColumn = curColumn; sumColumn < cols(); sumColumn++) {
				thisMatrix[curRow][sumColumn] += rowMultiplier * thisMatrix[curColumn][sumColumn];
			}
		}
	}
	for (size_t i = 0; i < (std::max)(cols(), rows()); i++) {
		general_float_type point_val = point.at(0, i);
		general_float_type value_at = thisMatrix.at(i, i);
		if (i >= cols() && ((std::abs)(point_val) > EPSILON))
			std::cerr << "Unresolveable OLE: 0X = " << point_val << std::endl;
		else if ((std::abs)(value_at) > EPSILON) {
			point_val /= value_at;
			answer.at(0, i) = point_val;
		}
	}
	return answer;
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::minor_matrix(const size_t& x_minor, const size_t& y_minor) const {
	auto minor_index = [](size_t x, size_t y, size_t minor_x, size_t minor_y) -> std::pair<int64_t, int64_t> {
		if (x == minor_x)
			return { -1,-1 };
		if (y == minor_y)
			return { -1,-1 };
		if (x > minor_x)
			x -= 1;
		if (y > minor_y)
			y -= 1;
		return { x,y };
	};
	self_type M;
	M.resize(rows() - 1, cols() - 1);
	for (size_t y = 0; y < rows(); y++) {
		for (size_t x = 0; x < cols(); x++) {
			auto mxy = minor_index(x, y, x_minor, y_minor);
			int64_t mx = mxy.first;
			int64_t my = mxy.second;
			if (mx < 0 || my < 0)
				continue;
			M.at(mx, my) = at(x, y);
		}
	}
	return M;
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::cross_prod(const self_type& points_in_matrix) {
	size_t dims = points_in_matrix.rows(); /* ex: [1,2] */
	if (points_in_matrix.rows() + 1 != points_in_matrix.cols())
		return {};
	if (dims > 0) {
		self_type answer;
		self_type mx = points_in_matrix;
		answer.resize(1, dims + 1);
		mx.resize(dims + 1, dims + 1);
		for (size_t i = dims; i >= 1; i--)
			mx[i] = mx[i - 1];
		for (size_t i = 0; i < dims + 1; i++)
			answer.at(i, 0) = (mx.minor_matrix(i, 0).determinant() * ((i & 1) ? (1.) : (-1.)));
		return answer;
	}
	else
		return {};
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::get_row(size_t row) const {
	return self_type({ this->operator[](row) });
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::get_col(size_t col) const {
	self_type new_mx;
	new_mx.resize(this->rows(), 1);
	for (size_t i = 0; i < new_mx.rows(); i++)
		new_mx[i][0] = at(col, i);
	return new_mx;
}

template<typename general_float_type>
inline matrix<general_float_type>& matrix<general_float_type>::set_row(size_t row, const self_type& mx_row) {
	if (cols() != mx_row.cols() || row >= rows())
		return *this;
	_matrix[row] = mx_row[0];
	return *this;
}

template<typename general_float_type>
inline matrix<general_float_type>& matrix<general_float_type>::set_col(size_t col, const self_type& mx_col) {
	if (rows() != mx_col.rows() || col >= cols())
		return *this;
	for (size_t i = 0; i < mx_col.rows(); i++)
		_matrix[i][col] = mx_col[i][0];
	return *this;
}

template<typename general_float_type>
inline matrix<general_float_type> operator*(general_float_type number, const matrix<general_float_type>& rightMatrix) {
	return rightMatrix * number;
}

template<typename general_float_type>
inline bool matrix<general_float_type>::operator==(const matrix<general_float_type>& rightMatrix) const {
	if (rows() != rightMatrix.rows() && cols() != rightMatrix.cols())
		return false;
	for (size_t y = 0; y < rows(); y++) {
		for (size_t x = 0; x < cols(); x++) {
			if (_matrix[y][x] != rightMatrix._matrix[y][x])
				return false;
		}
	}
	return true;
}

template<typename general_float_type>
inline std::istream& operator>>(std::istream& in, matrix<general_float_type>& rightMatrix) {
	size_t y, x;
	in >> y >> x;
	rightMatrix.resize(y, x);
	for (auto&& line : rightMatrix._matrix)
		for (auto&& digit : line)
			in >> digit;
	return in;
}

template<typename general_float_type>
inline std::ostream& operator<<(std::ostream& out, const matrix<general_float_type>& rightMatrix) {
	for (auto&& line : rightMatrix._matrix) {
		for (auto&& digit : line) {
			out << std::setprecision(6) << std::setw(15) << digit;
		}
		out << '\n';
	}
	return out;
}

template<typename general_float_type>
inline bool matrix<general_float_type>::operator<(const matrix<general_float_type>& comparation_m) const {
	if (rows() != comparation_m.rows() || cols() != comparation_m.cols())
		throw std::runtime_error("matrix sizes mismatch on inequality comparison");
	for (size_t y = 0; y < rows(); y++)
		for (size_t x = 0; x < cols(); x++)
			if (at(x, y) >= comparation_m.at(x, y))
				return false;
	return true;
}

template<typename general_float_type>
inline bool matrix<general_float_type>::operator<=(const self_type& comparation_m) const {
	if (rows() != comparation_m.rows() || cols() != comparation_m.cols())
		throw std::runtime_error("matrix sizes mismatch on inequality comparison");
	for (size_t y = 0; y < rows(); y++)
		for (size_t x = 0; x < cols(); x++)
			if (at(x, y) > comparation_m.at(x, y))
				return false;
	return true;
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::ppow(general_float_type p) const {
	self_type mx(*this);
	mx.selfppow(p);
	return mx;
}

template<typename general_float_type>
inline matrix<general_float_type>& matrix<general_float_type>::selfppow(general_float_type p) {
	for (auto& line : _matrix)
		for (auto& val : line)
			val = std::pow(val, p);
	return *this;
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::pabs() const {
	self_type mx(*this);
	mx.selfpabs();
	return mx;
}

template<typename general_float_type>
inline matrix<general_float_type>& matrix<general_float_type>::selfpabs() {
	for (auto& line : _matrix)
		for (auto& val : line)
			val = (std::abs)(val);
	return *this;
}

template<typename general_float_type>
inline general_float_type matrix<general_float_type>::psum() const {
	general_float_type s = 0;
	for (auto& line : _matrix)
		for (auto& val : line)
			s += val;
	return s;
}

template<typename general_float_type>
inline matrix<general_float_type>& matrix<general_float_type>::selfapply(const std::function<void(general_float_type&)>& func) {
	for (auto& line : _matrix)
		for (auto& val : line)
			func(val);
	return *this;
}

template<typename general_float_type>
inline void matrix<general_float_type>::call_indexed(const std::function<void(general_float_type, size_t, size_t)>& func) const {
	for (size_t y = 0; y < rows(); y++)
		for (size_t x = 0; x < cols(); x++)
			func(at(x, y), y, x);
}

template<typename general_float_type>
inline void matrix<general_float_type>::call(const std::function<void(general_float_type)>& func) const {
	for (auto& line : _matrix)
		for (auto& val : line)
			func(val);
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::apply(const std::function<void(general_float_type&)>& func) const {
	self_type mx(*this);
	mx.selfapply(func);
	return mx;
}

template<typename general_float_type>
inline matrix<general_float_type>& matrix<general_float_type>::selfapply_indexed(
	const std::function<void(general_float_type&, size_t, size_t)>& func) {
	for (size_t y = 0; y < rows(); y++)
		for (size_t x = 0; x < cols(); x++)
			func(at(x, y), y, x);
	return *this;
}

template<typename general_float_type>
inline matrix<general_float_type> matrix<general_float_type>::apply_indexed(
	const std::function<void(general_float_type&, size_t, size_t)>& func) const {
	self_type mx(*this);
	mx.selfapply_indexed(func);
	return mx;
}

template<typename general_float_type>
inline matrix<general_float_type>& matrix<general_float_type>::normalize(general_float_type p) {
	return *this /= norma(p);
}

template<typename general_float_type>
inline general_float_type matrix<general_float_type>::norma(general_float_type p) const {
	general_float_type sum = 0;
	for (auto& line : _matrix)
		for (auto& val : line)
			sum += std::pow((std::abs)(val), p);
	return std::pow(sum, 1. / p);
}

template<typename general_float_type>
inline std::pair<general_float_type, general_float_type> matrix<general_float_type>::minmax() const
{
	general_float_type min = std::numeric_limits<general_float_type>::max();
	general_float_type max = std::numeric_limits<general_float_type>::min();
	
	call([&min, &max](general_float_type v) {
		if (!std::isfinite(v))
			return;
		min = (std::min)(v, min);
		max = (std::max)(v, max);
	});

	if(min > max)
		throw std::runtime_error("matrix::minmax min > max");

	return {min, max};
}

} // namespace dixelu

#endif 
