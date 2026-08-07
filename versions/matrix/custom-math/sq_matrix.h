#ifndef _DIXELU_SQ_MATRIX_H_
#define _DIXELU_SQ_MATRIX_H_

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>
#include <limits>   
#include <ostream>
#include <iomanip>
#include <functional>
#include <type_traits>
#include <initializer_list>

#include "math_utils.h"

#if (defined(__cpp_constexpr) && (__cpp_constexpr >= 201304L))
#ifndef __DIXELU_RELAXED_CONSTEXPR
#define __DIXELU_RELAXED_CONSTEXPR constexpr
#endif
#else
#ifndef __DIXELU_RELAXED_CONSTEXPR
#define __DIXELU_RELAXED_CONSTEXPR
#endif
#endif

#ifndef __DIXELU_CONDITIONAL_CPP14_SPECIFIERS
#define __DIXELU_CONDITIONAL_CPP14_SPECIFIERS inline __DIXELU_RELAXED_CONSTEXPR
#endif

namespace dixelu
{
	template<typename general_float_type, std::size_t dims>
	struct point
	{
		general_float_type base_array[dims];
		using general_inttype = std::conditional<
			std::is_same<general_float_type, int>::value, long long int, int>;
		using self_type = point<general_float_type, dims>;
		__DIXELU_RELAXED_CONSTEXPR point() :
			base_array()
		{
			for (std::size_t i = 0; i < dims; ++i)
				base_array[i] = general_float_type(0);
		}
		__DIXELU_RELAXED_CONSTEXPR explicit point(general_float_type v) :
			base_array()
		{
			for (std::size_t i = 0; i < dims; ++i)
				base_array[i] = v;
		}
		__DIXELU_RELAXED_CONSTEXPR point(const std::initializer_list<general_inttype>& il_d) :
			base_array()
		{
			auto y = il_d.begin();
			for (std::size_t i = 0; i < dims && y != il_d.end(); ++i, ++y)
				base_array[i] = *y;
		}
		__DIXELU_RELAXED_CONSTEXPR point(const std::initializer_list<int>& il) :
			base_array()
		{
			auto y = il.begin();
			for (std::size_t i = 0; i < dims && y != il.end(); ++i, ++y)
				base_array[i] = *y;
		}
		__DIXELU_RELAXED_CONSTEXPR point(const std::vector<general_inttype>& il_d) :
			base_array()
		{
			auto y = il_d.cbegin();
			for (std::size_t i = 0; i < dims && y != il_d.end(); ++i, ++y)
				base_array[i] = *y;
		}
		__DIXELU_RELAXED_CONSTEXPR point(const std::vector<int>& il) :
			base_array()
		{
			auto y = il.cbegin();
			for (std::size_t i = 0; i < dims && y != il.end(); ++i, ++y)
				base_array[i] = *y;
		}
		__DIXELU_RELAXED_CONSTEXPR point(const general_float_type(&starr)[dims]) :
			base_array()
		{
			for (std::size_t i = 0; i < dims; ++i)
				base_array[i] = starr[i];
		}
		__DIXELU_RELAXED_CONSTEXPR point(const self_type& pt) :
			base_array()
		{
			for (std::size_t i = 0; i < dims; ++i)
				base_array[i] = pt[i];
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS general_float_type get_norm2() const
		{
			general_float_type sum = general_float_type();
			for (std::size_t i = 0; i < dims; ++i)
				sum += base_array[i] * base_array[i];
			return sum;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS general_float_type get_norm(general_float_type deg) const
		{
			general_float_type sum = general_float_type();
			for (std::size_t i = 0; i < dims; ++i)
				sum += utils::constexpr_pow<general_float_type>(base_array[i], deg);
			return utils::constexpr_pow<general_float_type>(sum, 1. / deg);
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS general_float_type get_norm() const
		{
			return utils::constexpr_sqrt(get_norm2());
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS std::size_t get_dims() const
		{
			return dims;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS general_float_type& operator[](std::size_t D)
		{
			return base_array[D];
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS const general_float_type& operator[](std::size_t D) const
		{
			return base_array[D];
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS void swap(self_type& P)
		{
			for (std::size_t i = 0; i < dims; ++i)
			{
				auto value = base_array[i];
				base_array[i] = P.base_array[i];
				P.base_array[i] = value;
			}
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type reverse() const
		{
			self_type N;
			for (std::size_t i = 0; i < dims; ++i)
				N[i] = base_array[dims - 1 - i];
			return N;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type operator+(const self_type& P) const
		{
			self_type N;
			for (std::size_t i = 0; i < dims; ++i)
				N[i] = base_array[i] + P[i];
			return N;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type& operator+=(const self_type& P)
		{
			for (std::size_t i = 0; i < dims; ++i)
				base_array[i] += P[i];
			return *this;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type operator-(const self_type& P) const
		{
			self_type N;
			for (std::size_t i = 0; i < dims; ++i)
				N[i] = base_array[i] - P[i];
			return N;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type& operator-=(const self_type& P)
		{
			for (std::size_t i = 0; i < dims; ++i)
				base_array[i] -= P[i];
			return *this;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type operator*(general_float_type M) const
		{
			self_type N;
			for (std::size_t i = 0; i < dims; ++i)
				N[i] = base_array[i] * M;
			return N;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type& operator*=(general_float_type M)
		{
			for (std::size_t i = 0; i < dims; ++i)
				base_array[i] *= M;
			return *this;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type operator/(general_float_type M) const
		{
			self_type N;
			for (std::size_t i = 0; i < dims; ++i)
				N[i] = base_array[i] / M;
			return N;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type& operator/=(general_float_type M)
		{
			return ((*this) *= (general_float_type(1) / M));
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS general_float_type operator*(const self_type& P) const
		{
			general_float_type sum = general_float_type();
			for (std::size_t i = 0; i < dims; ++i)
				sum += base_array[i] * P[i];
			return sum;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type& operator-()
		{
			for (std::size_t i = 0; i < dims; ++i)
				base_array[i] = 0 - base_array[i];
			return *this;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS bool operator<(const self_type& P) const
		{
			for (std::size_t i = 0; i < dims; ++i)
				if (base_array[i] >= P.base_array[i])
					return false;
			return true;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS bool operator>=(const self_type& P) const
		{
			return !(*this < P);
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS bool operator>(const self_type& P) const
		{
			return P < *this;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS bool operator==(const self_type& P) const
		{
			for (std::size_t i = 0; i < dims; ++i)
				if (base_array[i] != P[i]) return false;
			return true;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS bool operator!=(const self_type& P) const
		{
			return !(*this == P);
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS bool operator<=(const self_type& P) const
		{
			return !(P < *this);
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type normalize() const
		{
			return (*this) / get_norm();
		}
	};

	template<typename general_float_type, std::size_t dims>
	std::ostream& operator<<(std::ostream& os, const point<general_float_type, dims>& P)
	{
		os << "(";
		for (std::size_t i = 0; i < dims; ++i)
		{
			os << P[i];
			if (i != dims - 1)
				os << ",";
		}
		os << ")";
		return os;
	}

	template<typename general_float_type, std::size_t dims>
	__DIXELU_CONDITIONAL_CPP14_SPECIFIERS point<general_float_type, dims> operator*(general_float_type M, point<general_float_type, dims> P)
	{
		point<general_float_type, dims> N;
		for (std::size_t i = 0; i < dims; ++i)
			N[i] = P[i] * M;
		return N;
	}

	template<typename general_float_type, std::size_t dims>
	struct sq_matrix
	{
		//static constexpr general_float_type GFLOAT_EPSILON = std::numeric_limits<general_float_type>::epsilon();
		static constexpr std::size_t minor_type_size = (dims > 1) ? dims - 1 : 1;
		general_float_type utilisation = general_float_type();

		using point_type = point<general_float_type, dims>;
		using self_type = sq_matrix<general_float_type, dims>;
		using minor_type = sq_matrix<general_float_type, minor_type_size>;

		point_type base_array[dims];

		__DIXELU_RELAXED_CONSTEXPR sq_matrix() :
			base_array()
		{
			for (std::size_t i = 0; i < dims; ++i)
				base_array[i] = point_type();
		}
		__DIXELU_RELAXED_CONSTEXPR sq_matrix(const self_type& mx) :
			base_array()
		{
			for (std::size_t i = 0; i < dims; ++i)
				base_array[i] = mx[i];
		}
		__DIXELU_RELAXED_CONSTEXPR explicit sq_matrix(general_float_type E_num) :
			base_array()
		{
			for (std::size_t i = 0; i < dims; ++i)
			{
				base_array[i] = point_type();
				base_array[i][i] = E_num;
			}
		}
		__DIXELU_RELAXED_CONSTEXPR sq_matrix(const point_type(&matrix)[dims]) :
			base_array()
		{
			for (std::size_t i = 0; i < dims; ++i)
				base_array[i] = matrix[i];
		}
		__DIXELU_RELAXED_CONSTEXPR sq_matrix(std::initializer_list<point_type> IL) :
			base_array()
		{
			std::size_t id = 0;
			for (auto& p : IL)
			{
				if (id == dims)
					break;
				base_array[id] = p;
				++id;
			}
		}
		void swap(self_type& p)
		{
			for (std::size_t i = 0; i < dims; ++i)
				p[i].swap(base_array[i]);
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS point_type operator*(const point_type& p) const
		{
			point_type T;
			for (std::size_t i = 0; i < dims; ++i)
				T[i] = base_array[i] * p;
			return T;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type operator*(general_float_type num) const
		{
			self_type T;
			for (std::size_t i = 0; i < dims; ++i)
				T[i] = base_array[i] * num;
			return T;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type operator*=(general_float_type num)
		{
			for (std::size_t i = 0; i < dims; ++i)
				base_array[i] *= num;
			return *this;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type operator/(general_float_type num) const
		{
			return *this * (1. / num);
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type& operator/=(general_float_type num)
		{
			return ((*this) *= (1. / num));
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type operator+(const self_type& p) const {
			self_type T;
			for (std::size_t i = 0; i < dims; ++i)
				T[i] = base_array[i] + p[i];
			return T;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type operator-(const self_type& p) const
		{
			self_type T;
			for (std::size_t i = 0; i < dims; ++i)
				T[i] = base_array[i] - p[i];
			return T;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type& operator+=(const self_type& p)
		{
			for (std::size_t i = 0; i < dims; ++i)
				base_array[i] += p[i];
			return *this;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type& operator-=(const self_type& p)
		{
			for (std::size_t i = 0; i < dims; ++i)
				base_array[i] -= p[i];
			return *this;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS const point_type& operator[](std::size_t i) const
		{
			return base_array[i];
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS point_type& operator[](std::size_t i)
		{
			return base_array[i];
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS general_float_type& at(std::size_t point_id, std::size_t coordinate)
		{
			if (point_id < dims && coordinate < dims)
			{
				return base_array[point_id][coordinate];
			}
			else return utilisation;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS const general_float_type& at(std::size_t point_id, std::size_t coordinate) const
		{
			if (point_id < dims && coordinate < dims)
			{
				return base_array[point_id][coordinate];
			}
			else return utilisation;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type operator*(const self_type& M) const
		{
			self_type P;
			for (std::size_t x = 0; x < dims; ++x)
			{
				for (std::size_t y = 0; y < dims; ++y)
				{
					P.base_array[y][x] = general_float_type(0);
					for (std::size_t i = 0; i < dims; ++i)
					{
						P[y][x] += base_array[y][i] * M[i][x];
					}
				}
			}
			return P;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type inverse() const
		{
			general_float_type max_value = general_float_type();
			general_float_type mul = general_float_type();
			std::size_t id = 0;
			self_type E(1), A(*this);
			for (std::size_t step = 0; step < dims; ++step)
			{
				id = step;
				max_value = 0;
				for (std::size_t coid = step; coid < dims; ++coid)
				{
					if (utils::constexpr_abs(max_value) < utils::constexpr_abs(A.at(coid, step)))
					{
						max_value = A.at(coid, step);
						id = coid;
					}
				}
				if (utils::constexpr_abs(max_value) <= std::numeric_limits<general_float_type>::epsilon())
					return self_type();
				if (id != step)
				{
					A[id].swap(A[step]);
					E[id].swap(E[step]);
				}
				for (std::size_t sum_id = 0; sum_id < dims; ++sum_id)
				{
					if (sum_id == step)
						continue;
					mul = A.at(sum_id, step) / A.at(step, step);
					A[sum_id] -= (A[step] * mul);
					E[sum_id] -= (E[step] * mul);
				}
				mul = A.at(step, step);
				A[step] /= mul;
				E[step] /= mul;
			}
			return E;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS general_float_type determinant() const
		{
			general_float_type determ = 1;
			general_float_type max_value = 0, mul = 0;
			std::size_t id = 0;
			self_type A(*this);
			for (std::size_t step = 0; step < dims; ++step)
			{
				id = step;
				max_value = 0;
				for (std::size_t coid = step; coid < dims; ++coid)
				{
					if (utils::constexpr_abs(max_value) < utils::constexpr_abs(A.at(coid, step)))
					{
						max_value = A.at(coid, step);
						id = coid;
					}
				}
				if (utils::constexpr_abs(max_value) <= std::numeric_limits<general_float_type>::epsilon())
					return 0;
				if (id != step)
					A[id].swap(A[step]);
				for (std::size_t sum_id = 0; sum_id < dims; ++sum_id)
				{
					if (sum_id == step)
						continue;
					mul = A.at(sum_id, step) / A.at(step, step);
					A[sum_id] -= (A[step] * mul);
				}
				mul = A.at(step, step);
				A[step] /= mul;
				determ *= mul;
			}
			return determ;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS static point_type solve_using_eulers_method(self_type A, point_type P)
		{
			general_float_type max_value = 0, mul = 0;
			std::size_t id = 0;
			for (std::size_t step = 0; step < dims; ++step)
			{
				id = step;
				max_value = 0;
				for (std::size_t coid = step; coid < dims; ++coid)
				{
					if (utils::constexpr_abs(max_value) < utils::constexpr_abs(A.at(coid, step)))
					{
						max_value = A.at(coid, step);
						id = coid;
					}
				}
				if (utils::constexpr_abs(max_value) <= std::numeric_limits<general_float_type>::epsilon())
					return point_type();
				if (id != step)
				{
					A[id].swap(A[step]);
					P[id].swap(P[step]);
				}
				for (std::size_t sum_id = 0; sum_id < dims; ++sum_id)
				{
					if (sum_id == step)
						continue;
					mul = A.at(sum_id, step) / A.at(step, step);
					A[sum_id] -= A[step] * mul;
					P[sum_id] -= P[step] * mul;
				}
				mul = A.at(step, step);
				A[step] /= mul;
				P[step] /= mul;
			}
			return P;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type operator^(int degree)
		{
			bool inverse = false;
			unsigned int udegree = degree;
			if (degree < 0)
			{
				inverse = true;
				udegree = -degree;
			}
			else if (!degree)
				return self_type(1);
			self_type cur_matrix(1), deg_co_matrix(*this);
			while (udegree)
			{
				switch (udegree & 1)
				{
				case 1:
					cur_matrix = cur_matrix * deg_co_matrix;
					deg_co_matrix = deg_co_matrix * deg_co_matrix;
					break;
				case 0:
					deg_co_matrix = deg_co_matrix * deg_co_matrix;
					break;
				}
				udegree >>= 1;
			}
			return inverse ? cur_matrix.inverse() : cur_matrix;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type& operator^=(int degree)
		{
			return ((*this) = (*this) ^ degree), *this;
		}
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS minor_type
			minor_matrix(const std::size_t& x_minor, const std::size_t& y_minor) const
		{
			auto minor_index = [](std::size_t x, std::size_t y, std::size_t minor_x, std::size_t minor_y) ->
				std::pair<int64_t, int64_t>
			{
				if (x == minor_x)
					return { -1, -1 };
				if (y == minor_y)
					return { -1, -1 };
				if (x > minor_x)
					x -= 1;
				if (y > minor_y)
					y -= 1;
				return { x, y };
			};
			minor_type M;
			for (std::size_t y = 0; y < dims; ++y)
			{
				for (std::size_t x = 0; x < dims; ++x)
				{
					auto mxy = minor_index(x, y, x_minor, y_minor);
					int64_t mx = mxy.first;
					int64_t my = mxy.second;
					if (mx < 0 || my < 0)
						continue;
					M.at(my, mx) = base_array[y][x];
				}
			}
			return M;
		}

		template<std::size_t new_dims>
		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS sq_matrix<general_float_type, new_dims>
			to(std::size_t start_index = 0)
		{
			sq_matrix<general_float_type, new_dims> new_matrix;

			const auto max_iter_pos = utils::constexpr_min(dims, new_dims - start_index);

			for (std::size_t y = start_index; y < max_iter_pos; ++y)
				for (std::size_t x = start_index; x < max_iter_pos; ++x)
				{
					new_matrix[y - start_index][x - start_index] =
						base_array[y][x];
				}

			return new_matrix;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type& transpose()
		{
			for (std::size_t y = 0; y < dims; ++y)
				for (std::size_t x = 0; x < dims; ++x)
				{
					auto t = base_array[y][x];
					base_array[y][x] = base_array[x][y];
					base_array[x][y] = t;
				}
			return *this;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS general_float_type trace() const
		{
			general_float_type sum(0);
			for (std::size_t i = 0; i < dims; ++i)
				sum += base_array[i][i];
			return sum;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type ppow(double p) const
		{
			self_type mx(*this);
			mx.selfppow(p);
			return mx;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type& selfppow(double p)
		{
			for (std::size_t y = 0; y < dims; ++y)
				for (std::size_t x = 0; x < dims; ++x)
					base_array[y][x] = utils::constexpr_pow(base_array[y][x]);
			return *this;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type pabs() const
		{
			self_type mx(*this);
			mx.selfpabs();
			return mx;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type& selfpabs()
		{
			for (std::size_t y = 0; y < dims; ++y)
				for (std::size_t x = 0; x < dims; ++x)
					base_array[y][x] = utils::constexpr_abs(base_array[y][x]);
			return *this;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS general_float_type psum() const
		{
			general_float_type s = general_float_type(0);
			for (std::size_t y = 0; y < dims; ++y)
				for (std::size_t x = 0; x < dims; ++x)
					s += base_array[y][x];
			return s;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type& selfapply(const std::function<void(general_float_type&)>& func)
		{
			for (std::size_t y = 0; y < dims; ++y)
				for (std::size_t x = 0; x < dims; ++x)
					func(base_array[y][x]);
			return *this;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type apply(const std::function<void(general_float_type&)>& func) const
		{
			self_type mx(*this);
			mx.selfapply(func);
			return mx;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type& selfapply_indexed(const std::function<void(general_float_type&, size_t, size_t)>& func)
		{
			for (std::size_t y = 0; y < dims; ++y)
				for (std::size_t x = 0; x < dims; ++x)
					func(base_array[y][x], y, x);
			return *this;
		}

		__DIXELU_CONDITIONAL_CPP14_SPECIFIERS self_type apply_indexed(const std::function<void(general_float_type&, size_t, size_t)>& func) const
		{
			self_type mx(*this);
			mx.selfapply_indexed(func);
			return mx;
		}
	};

	template<typename general_float_type, std::size_t dims>
	__DIXELU_CONDITIONAL_CPP14_SPECIFIERS point<general_float_type, dims>
		cross_prod(const std::array<point<general_float_type, dims>,
			sq_matrix<general_float_type, dims>::minor_type_size>& points)
	{
		if (dims > 1)
		{
			point<general_float_type, dims> answer;
			std::array<point<general_float_type, dims>, dims> mx;
			mx[0] = point<general_float_type, dims>(std::vector<general_float_type>(dims, 0));
			std::copy(points.begin(), points.end(), mx.begin() + 1);
			sq_matrix<general_float_type, dims> M(mx);
			for (int i = 0; i < dims; ++i)
				answer[i] = M.minor_matrix(i, 0).determinant() * ((i & 1) ? (1.) : (-1.));
			return answer;
		}
		else if (dims == 1)
			return { 0 };
		else
			return {};
	}

	template<size_t dims, typename general_float_type>
	__DIXELU_CONDITIONAL_CPP14_SPECIFIERS std::ostream& operator<<(std::ostream& in,
		const sq_matrix<general_float_type, dims>& M)
	{
		for (size_t y = 0; y < dims; ++y)
		{
			for (size_t x = 0; x < dims; ++x)
			{
				in << std::setfill(' ') << std::setw(15) << M.at(y, x) << " ";
			}
			in << "\n";
		}
		return in;
	}
} // namespace dixelu

#endif // _DIXELU_SQ_MATRIX_H_