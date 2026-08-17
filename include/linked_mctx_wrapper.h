// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Alexander Verevkin
#pragma once

#include "mctx_path.h"

#include <memory>
#include <optional>
#include <string>

namespace dixelu
{

class linked_mctx_wrapper
{
public:
	class trivial_pseudo_iterator;

	linked_mctx_wrapper() noexcept = default;
	explicit linked_mctx_wrapper(std::shared_ptr<mctx> root_mctx);

	// Cloning intentionally resets the selected node to the cloned root.
	[[nodiscard]] linked_mctx_wrapper deep_clone() const;

	[[nodiscard]] bool is_valid() const noexcept { return _current_node != nullptr; }
	[[nodiscard]] bool empty() const noexcept
	{
		return _current_node == nullptr || !_root_mctx;
	}
	[[nodiscard]] bool owns_settings_instance() const noexcept
	{
		return _root_mctx && _root_mctx.use_count() == 1;
	}

	[[nodiscard]] linked_mctx_wrapper operator[](std::string key) const;
	[[nodiscard]] linked_mctx_wrapper operator[](mctx_path path) const;
	[[nodiscard]] linked_mctx_wrapper at(std::string key) const;
	[[nodiscard]] linked_mctx_wrapper at(mctx_path path) const;

	template<typename T>
	[[nodiscard]] T get_as(std::string key, T default_value) const
	{
		return operator[](std::move(key)).get_as<T>(std::move(default_value));
	}

	template<typename T>
	[[nodiscard]] T get_as(T default_value) const
	{
		if(_current_node)
			return _current_node->get_as<T>(std::move(default_value));
		return default_value;
	}

	template<typename T>
	[[nodiscard]] const T& as() const
	{
		if(_current_node && _current_node->is<T>())
			return _current_node->as<T>();
		throw std::runtime_error("linked_mctx_wrapper has no value of the requested type");
	}

	template<typename T>
	[[nodiscard]] const T& as(const T& substitute) const noexcept
	{
		if(_current_node && _current_node->is<T>())
			return _current_node->as<T>();
		return substitute;
	}

	[[nodiscard]] const linked_mctx_wrapper* operator->() const noexcept { return this; }

	[[nodiscard]] trivial_pseudo_iterator begin() const;
	[[nodiscard]] trivial_pseudo_iterator end() const;
	[[nodiscard]] trivial_pseudo_iterator find(const std::string& key) const;

	[[nodiscard]] mctx render_as_linked_mctx() const;
	// Diff nodes use the legacy MetaSDK shape: none means unchanged, ordinary
	// objects/arrays recurse, {"-": old} deletes, and {"+": new, "-": old}
	// replaces. By default changes to rendered references are forwarded to their
	// targets; force_no_diff_forwarding edits the reference object itself.
	void apply_rendered_mctx_diff(const mctx& diff, bool force_no_diff_forwarding = false) const;

	void clear() noexcept
	{
		_current_node = nullptr;
		_root_mctx.reset();
	}

	[[nodiscard]] const mctx& get_underlying_mctx_root() const;
	[[nodiscard]] const mctx& get_underlying_mctx_node() const;

	class trivial_pseudo_iterator
	{
	public:
		trivial_pseudo_iterator() = default;

		trivial_pseudo_iterator& operator++();
		trivial_pseudo_iterator operator++(int);
		trivial_pseudo_iterator& operator--();
		trivial_pseudo_iterator operator--(int);

		[[nodiscard]] bool operator==(const trivial_pseudo_iterator& other) const;
		[[nodiscard]] bool operator!=(const trivial_pseudo_iterator& other) const
		{
			return !(*this == other);
		}

		[[nodiscard]] linked_mctx_wrapper operator*() const;
		[[nodiscard]] linked_mctx_wrapper operator->() const;

	private:
		trivial_pseudo_iterator(
			std::shared_ptr<mctx> root_mctx,
			const mctx* owner,
			mctx::value_iter iterator);

		std::optional<mctx::value_iter> _iterator;
		std::shared_ptr<mctx> _root;
		const mctx* _owner = nullptr;

		friend class linked_mctx_wrapper;
	};

private:
	[[nodiscard]] linked_mctx_wrapper make_shallow_copy() const;
	[[nodiscard]] linked_mctx_wrapper get_subfield(std::string key) const;
	[[nodiscard]] mctx_path::small_vector<linked_mctx_wrapper> get_by_path(mctx_path& path) const;

	[[nodiscard]] const mctx* get_linked_mctx(
		const mctx* node_mctx,
		bool& is_reference) const;
	[[nodiscard]] mctx_path::small_vector<const mctx*> get_all_linked_mctxs(
		const mctx* node_mctx,
		bool& is_reference) const;

	[[nodiscard]] mctx render_mctx_node(
		const mctx* node,
		mctx_path::small_vector<const mctx*>& active_links,
		std::size_t depth) const;
	bool apply_diff_to_node(
		mctx& node,
		const mctx& diff,
		bool force_no_diff_forwarding) const;

	static constexpr const char* ref_tag = "$ref";
	static constexpr const char* array_tag = "$as_array";
	static constexpr std::size_t max_link_depth = 64;

	std::shared_ptr<mctx> _root_mctx;
	const mctx* _current_node = nullptr;
};

} // namespace dixelu
