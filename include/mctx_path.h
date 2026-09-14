// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Alexander Verevkin
#pragma once

#include "mctx.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace dixelu
{

class mctx_path_serializer;
class linked_mctx_wrapper;

/// A composable, value-type path for selecting nodes in an mctx tree.
///
/// Key, index, root, previous and variable segments navigate the tree. A
/// selector segment expands object or array children; it can use a predicate
/// or a set of query clauses. Every clause must match the selected child, and
/// an equality clause matches when any node selected by its relative path has
/// the expected scalar value. `get_all_matching()` retains every expanded
/// result, while `operator[]` and `try_get()` select the first result.
///
/// `stringify()` produces the canonical mctx_path text form. Variable bindings
/// and programmatic predicates are runtime state and are not serialized.
class mctx_path
{
public:
	template<typename T>
	using small_vector = std::vector<T>;

	struct variable_tag {};
	struct previous_tag {};
	struct root_tag {};

	static constexpr variable_tag variable{};
	static constexpr previous_tag previous{};
	static constexpr root_tag root{};

	class query;

	mctx_path();
	explicit mctx_path(std::string key);
	explicit mctx_path(const char* key);
	explicit mctx_path(std::size_t index);
	template<std::integral integer>
		requires (!std::same_as<std::remove_cv_t<integer>, bool> &&
			 !std::same_as<std::remove_cv_t<integer>, std::size_t>)
	explicit mctx_path(integer index): mctx_path()
	{
		using integer_type = std::remove_cv_t<integer>;
		if constexpr(std::is_signed_v<integer_type>)
		{
			if(index < 0)
				throw std::out_of_range("mctx_path indices cannot be negative");
		}
		using unsigned_type = std::make_unsigned_t<integer_type>;
		const auto unsigned_index = static_cast<unsigned_type>(index);
		if(unsigned_index > std::numeric_limits<std::size_t>::max())
			throw std::out_of_range("mctx_path index does not fit size_t");
		append_index(static_cast<std::size_t>(unsigned_index));
	}
	explicit mctx_path(root_tag);
	explicit mctx_path(variable_tag);
	explicit mctx_path(previous_tag);

	mctx_path(const mctx_path&);
	mctx_path(mctx_path&&) noexcept;
	mctx_path& operator=(const mctx_path&);
	mctx_path& operator=(mctx_path&&) noexcept;
	~mctx_path();

	[[nodiscard]] mctx_path copy() const { return *this; }
	[[nodiscard]] std::size_t size() const noexcept;
	[[nodiscard]] bool empty() const noexcept;

	[[nodiscard]] mctx& operator[](mctx& target) const;
	[[nodiscard]] const mctx& operator[](const mctx& target) const;
	[[nodiscard]] const mctx* try_get(const mctx& target) const;
	[[nodiscard]] const mctx* get_conditional(const mctx& target) const
	{
		return try_get(target);
	}

	[[nodiscard]] bool exists(const mctx& target) const;
	// Any/All expand wildcard, predicate, and query-set segments. All uses
	// vacuous truth when a branching segment has no matches.
	[[nodiscard]] bool exists_any(const mctx& target) const;
	[[nodiscard]] bool exists_all(const mctx& target) const;
	// Removes every destination selected by a branching path. Empty ancestors
	// are retained, so removal never silently changes a parent container's shape.
	[[nodiscard]] bool remove(mctx& target) const;
	[[nodiscard]] std::string stringify() const;

	template<template<class...> class container = std::deque>
	[[nodiscard]] container<const mctx*> get_all_matching(const mctx& target) const
	{
		container<const mctx*> result;
		for(const auto* match : get_all_matches(target))
			result.emplace_back(match);
		return result;
	}

	template<typename... value_types>
	void set_variables(value_types&&... values)
	{
		std::size_t search_from = 0;
		(set_variable_argument(search_from, std::forward<value_types>(values)), ...);
	}

	[[nodiscard]] static query query_equals(mctx_path relative_path, mctx expected);
	[[nodiscard]] static query query_exists(mctx_path relative_path);
	[[nodiscard]] static mctx_path where_all(std::initializer_list<query> queries);
	[[nodiscard]] static mctx_path where_all(std::vector<query> queries);

	template<typename predicate_type>
		requires std::predicate<std::decay_t<predicate_type>&, const mctx&>
	[[nodiscard]] static mctx_path where(predicate_type&& predicate)
	{
		return make_predicate(std::function<bool(const mctx&)>(
			std::forward<predicate_type>(predicate)));
	}

	friend mctx_path operator/(mctx_path path, std::string key);
	friend mctx_path operator/(mctx_path path, const char* key);

	template<std::integral integer>
		requires (!std::same_as<std::remove_cv_t<integer>, bool>)
	friend mctx_path operator/(mctx_path path, integer index)
	{
		using integer_type = std::remove_cv_t<integer>;
		if constexpr(std::is_signed_v<integer_type>)
		{
			if(index < 0)
				throw std::out_of_range("mctx_path indices cannot be negative");
		}

		using unsigned_type = std::make_unsigned_t<integer_type>;
		const auto unsigned_index = static_cast<unsigned_type>(index);
		if(unsigned_index > std::numeric_limits<std::size_t>::max())
			throw std::out_of_range("mctx_path index does not fit size_t");

		path.append_index(static_cast<std::size_t>(unsigned_index));
		return path;
	}

	friend mctx_path operator/(mctx_path path, const mctx_path& suffix);
	friend mctx_path operator/(mctx_path path, mctx_path&& suffix);
	friend mctx_path operator/(mctx_path path, variable_tag);
	friend mctx_path operator/(mctx_path path, previous_tag);
	friend mctx_path operator/(mctx_path path, root_tag);

private:
	struct impl;
	std::unique_ptr<impl> _impl;

	[[nodiscard]] static mctx_path make_predicate(
		std::function<bool(const mctx&)> predicate);

	void append_key(std::string key);
	void append_index(std::size_t index);
	void append_variable();
	void append_previous();
	void append_root();
	void append_path(const mctx_path& suffix);
	void append_path(mctx_path&& suffix);

	void set_variable(std::size_t& search_from, std::string key);
	void set_variable(std::size_t& search_from, std::size_t index);

	void set_variable_argument(std::size_t& search_from, std::string value)
	{
		set_variable(search_from, std::move(value));
	}

	void set_variable_argument(std::size_t& search_from, const char* value)
	{
		if(value == nullptr)
			throw std::invalid_argument("mctx_path variable key cannot be null");
		set_variable(search_from, std::string(value));
	}

	template<std::integral integer>
		requires (!std::same_as<std::remove_cv_t<integer>, bool>)
	void set_variable_argument(std::size_t& search_from, integer value)
	{
		using integer_type = std::remove_cv_t<integer>;
		if constexpr(std::is_signed_v<integer_type>)
		{
			if(value < 0)
				throw std::out_of_range("mctx_path variable index cannot be negative");
		}

		using unsigned_type = std::make_unsigned_t<integer_type>;
		const auto unsigned_value = static_cast<unsigned_type>(value);
		if(unsigned_value > std::numeric_limits<std::size_t>::max())
			throw std::out_of_range("mctx_path variable index does not fit size_t");
		set_variable(search_from, static_cast<std::size_t>(unsigned_value));
	}

	[[nodiscard]] small_vector<const mctx*> get_all_matches(const mctx& target) const;
	[[nodiscard]] bool exists_all_recursive(const mctx& target, std::size_t index) const;
	[[nodiscard]] bool remove_recursive(mctx& target, std::size_t index) const;
	void set_root_mctx(const std::shared_ptr<mctx>& root_mctx) const;

	friend class mctx_path_serializer;
	friend class linked_mctx_wrapper;
};

class mctx_path::query
{
public:
	query(const query&);
	query(query&&) noexcept;
	query& operator=(const query&);
	query& operator=(query&&) noexcept;
	~query();

private:
	query(mctx_path relative_path, std::unique_ptr<mctx> expected);

	mctx_path _relative_path;
	std::unique_ptr<mctx> _expected;

	friend class mctx_path;
};

} // namespace dixelu
