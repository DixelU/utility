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

class ContextPathSerializer;
class LinkedContextWrapper;

class ContextPath
{
public:
	template<typename T>
	using SmallVector = std::vector<T>;

	struct VariableTag {};
	struct PreviousTag {};
	struct RootTag {};

	static constexpr VariableTag variable{};
	static constexpr PreviousTag previous{};
	static constexpr RootTag root{};
	// Compatibility with the archived SAF-MTQ spelling.
	static constexpr RootTag $root{};

	class Query;

	ContextPath();
	explicit ContextPath(std::string key);
	explicit ContextPath(const char* key);
	explicit ContextPath(std::size_t index);
	template<std::integral Integer>
		requires (!std::same_as<std::remove_cv_t<Integer>, bool> &&
			 !std::same_as<std::remove_cv_t<Integer>, std::size_t>)
	explicit ContextPath(Integer index): ContextPath()
	{
		using integer_type = std::remove_cv_t<Integer>;
		if constexpr(std::is_signed_v<integer_type>)
		{
			if(index < 0)
				throw std::out_of_range("ContextPath indices cannot be negative");
		}
		using unsigned_type = std::make_unsigned_t<integer_type>;
		const auto unsigned_index = static_cast<unsigned_type>(index);
		if(unsigned_index > std::numeric_limits<std::size_t>::max())
			throw std::out_of_range("ContextPath index does not fit size_t");
		append_index(static_cast<std::size_t>(unsigned_index));
	}
	explicit ContextPath(RootTag);
	explicit ContextPath(VariableTag);
	explicit ContextPath(PreviousTag);

	ContextPath(const ContextPath&);
	ContextPath(ContextPath&&) noexcept;
	ContextPath& operator=(const ContextPath&);
	ContextPath& operator=(ContextPath&&) noexcept;
	~ContextPath();

	[[nodiscard]] ContextPath copy() const { return *this; }
	[[nodiscard]] std::size_t size() const noexcept;
	[[nodiscard]] bool empty() const noexcept;

	[[nodiscard]] mctx& operator[](mctx& target) const;
	[[nodiscard]] const mctx& operator[](const mctx& target) const;
	[[nodiscard]] const mctx* try_get(const mctx& target) const;
	[[nodiscard]] const mctx* getConditional(const mctx& target) const
	{
		return try_get(target);
	}

	[[nodiscard]] bool exists(const mctx& target) const;
	// Any/All expand wildcard, predicate, and query-set segments. All uses
	// vacuous truth when a branching segment has no matches.
	[[nodiscard]] bool existsAny(const mctx& target) const;
	[[nodiscard]] bool existsAll(const mctx& target) const;
	// Removes every destination selected by a branching path. Empty ancestors
	// are retained, so removal never silently changes a parent container's shape.
	[[nodiscard]] bool remove(mctx& target) const;
	[[nodiscard]] std::string stringify() const;

	template<template<class...> class Container = std::deque>
	[[nodiscard]] Container<const mctx*> getAllMatching(const mctx& target) const
	{
		Container<const mctx*> result;
		for(const auto* match : get_all_matching(target))
			result.emplace_back(match);
		return result;
	}

	template<typename... Values>
	void setVariables(Values&&... values)
	{
		std::size_t search_from = 0;
		(set_variable_argument(search_from, std::forward<Values>(values)), ...);
	}

	[[nodiscard]] static Query queryEquals(ContextPath relative_path, mctx expected);
	[[nodiscard]] static Query queryExists(ContextPath relative_path);
	[[nodiscard]] static ContextPath whereAll(std::initializer_list<Query> queries);
	[[nodiscard]] static ContextPath whereAll(std::vector<Query> queries);

	template<typename Predicate>
		requires std::predicate<std::decay_t<Predicate>&, const mctx&>
	[[nodiscard]] static ContextPath where(Predicate&& predicate)
	{
		return make_predicate(std::function<bool(const mctx&)>(
			std::forward<Predicate>(predicate)));
	}

	friend ContextPath operator/(ContextPath path, std::string key);
	friend ContextPath operator/(ContextPath path, const char* key);

	template<std::integral Integer>
		requires (!std::same_as<std::remove_cv_t<Integer>, bool>)
	friend ContextPath operator/(ContextPath path, Integer index)
	{
		using integer_type = std::remove_cv_t<Integer>;
		if constexpr(std::is_signed_v<integer_type>)
		{
			if(index < 0)
				throw std::out_of_range("ContextPath indices cannot be negative");
		}

		using unsigned_type = std::make_unsigned_t<integer_type>;
		const auto unsigned_index = static_cast<unsigned_type>(index);
		if(unsigned_index > std::numeric_limits<std::size_t>::max())
			throw std::out_of_range("ContextPath index does not fit size_t");

		path.append_index(static_cast<std::size_t>(unsigned_index));
		return path;
	}

	friend ContextPath operator/(ContextPath path, const ContextPath& suffix);
	friend ContextPath operator/(ContextPath path, ContextPath&& suffix);
	friend ContextPath operator/(ContextPath path, VariableTag);
	friend ContextPath operator/(ContextPath path, PreviousTag);
	friend ContextPath operator/(ContextPath path, RootTag);

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;

	[[nodiscard]] static ContextPath make_predicate(
		std::function<bool(const mctx&)> predicate);

	void append_key(std::string key);
	void append_index(std::size_t index);
	void append_variable();
	void append_previous();
	void append_root();
	void append_path(const ContextPath& suffix);
	void append_path(ContextPath&& suffix);

	void set_variable(std::size_t& search_from, std::string key);
	void set_variable(std::size_t& search_from, std::size_t index);

	void set_variable_argument(std::size_t& search_from, std::string value)
	{
		set_variable(search_from, std::move(value));
	}

	void set_variable_argument(std::size_t& search_from, const char* value)
	{
		if(value == nullptr)
			throw std::invalid_argument("ContextPath variable key cannot be null");
		set_variable(search_from, std::string(value));
	}

	template<std::integral Integer>
		requires (!std::same_as<std::remove_cv_t<Integer>, bool>)
	void set_variable_argument(std::size_t& search_from, Integer value)
	{
		using integer_type = std::remove_cv_t<Integer>;
		if constexpr(std::is_signed_v<integer_type>)
		{
			if(value < 0)
				throw std::out_of_range("ContextPath variable index cannot be negative");
		}

		using unsigned_type = std::make_unsigned_t<integer_type>;
		const auto unsigned_value = static_cast<unsigned_type>(value);
		if(unsigned_value > std::numeric_limits<std::size_t>::max())
			throw std::out_of_range("ContextPath variable index does not fit size_t");
		set_variable(search_from, static_cast<std::size_t>(unsigned_value));
	}

	[[nodiscard]] SmallVector<const mctx*> get_all_matching(const mctx& target) const;
	[[nodiscard]] bool exists_all_recursive(const mctx& target, std::size_t index) const;
	[[nodiscard]] bool remove_recursive(mctx& target, std::size_t index) const;
	void set_root_context(const std::shared_ptr<mctx>& root_context) const;

	friend class ContextPathSerializer;
	friend class LinkedContextWrapper;
};

class ContextPath::Query
{
public:
	Query(const Query&);
	Query(Query&&) noexcept;
	Query& operator=(const Query&);
	Query& operator=(Query&&) noexcept;
	~Query();

private:
	Query(ContextPath relative_path, std::unique_ptr<mctx> expected);

	ContextPath _relative_path;
	std::unique_ptr<mctx> _expected;

	friend class ContextPath;
};

} // namespace dixelu
