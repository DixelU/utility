// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Alexander Verevkin
#pragma once

#include "context_path.h"

#include <memory>
#include <optional>
#include <string>

namespace dixelu
{

class LinkedContextWrapper
{
public:
	class TrivialPseudoIterator;

	LinkedContextWrapper() noexcept = default;
	explicit LinkedContextWrapper(std::shared_ptr<mctx> root_context);

	// Cloning intentionally resets the selected node to the cloned root.
	[[nodiscard]] LinkedContextWrapper deepClone() const;

	[[nodiscard]] bool isValid() const noexcept { return _current_node != nullptr; }
	[[nodiscard]] bool empty() const noexcept
	{
		return _current_node == nullptr || !_root_context;
	}
	[[nodiscard]] bool ownsSettingsInstance() const noexcept
	{
		return _root_context && _root_context.use_count() == 1;
	}

	[[nodiscard]] LinkedContextWrapper operator[](std::string key) const;
	[[nodiscard]] LinkedContextWrapper operator[](ContextPath path) const;
	[[nodiscard]] LinkedContextWrapper at(std::string key) const;
	[[nodiscard]] LinkedContextWrapper at(ContextPath path) const;

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
		throw std::runtime_error("LinkedContextWrapper has no value of the requested type");
	}

	template<typename T>
	[[nodiscard]] const T& as(const T& substitute) const noexcept
	{
		if(_current_node && _current_node->is<T>())
			return _current_node->as<T>();
		return substitute;
	}

	[[nodiscard]] const LinkedContextWrapper* operator->() const noexcept { return this; }

	[[nodiscard]] TrivialPseudoIterator begin() const;
	[[nodiscard]] TrivialPseudoIterator end() const;
	[[nodiscard]] TrivialPseudoIterator find(const std::string& key) const;

	[[nodiscard]] mctx renderAsLinkedContext() const;
	// Diff nodes use the legacy MetaSDK shape: none means unchanged, ordinary
	// objects/arrays recurse, {"-": old} deletes, and {"+": new, "-": old}
	// replaces. By default changes to rendered references are forwarded to their
	// targets; force_no_diff_forwarding edits the reference object itself.
	void applyRenderedContextDiff(const mctx& diff, bool force_no_diff_forwarding = false) const;

	void clear() noexcept
	{
		_current_node = nullptr;
		_root_context.reset();
	}

	[[nodiscard]] const mctx& getUnderlyingContextRoot() const;
	[[nodiscard]] const mctx& getUnderlyingContextNode() const;

	class TrivialPseudoIterator
	{
	public:
		TrivialPseudoIterator() = default;

		TrivialPseudoIterator& operator++();
		TrivialPseudoIterator operator++(int);
		TrivialPseudoIterator& operator--();
		TrivialPseudoIterator operator--(int);

		[[nodiscard]] bool operator==(const TrivialPseudoIterator& other) const;
		[[nodiscard]] bool operator!=(const TrivialPseudoIterator& other) const
		{
			return !(*this == other);
		}

		[[nodiscard]] LinkedContextWrapper operator*() const;
		[[nodiscard]] LinkedContextWrapper operator->() const;

	private:
		TrivialPseudoIterator(
			std::shared_ptr<mctx> root_context,
			const mctx* owner,
			mctx::value_iter iterator);

		std::optional<mctx::value_iter> _iterator;
		std::shared_ptr<mctx> _root;
		const mctx* _owner = nullptr;

		friend class LinkedContextWrapper;
	};

private:
	[[nodiscard]] LinkedContextWrapper make_shallow_copy() const;
	[[nodiscard]] LinkedContextWrapper get_subfield(std::string key) const;
	[[nodiscard]] ContextPath::SmallVector<LinkedContextWrapper> get_by_path(ContextPath& path) const;

	[[nodiscard]] const mctx* get_linked_context(
		const mctx* node_context,
		bool& is_reference) const;
	[[nodiscard]] ContextPath::SmallVector<const mctx*> get_all_linked_contexts(
		const mctx* node_context,
		bool& is_reference) const;

	[[nodiscard]] mctx render_node(
		const mctx* node,
		ContextPath::SmallVector<const mctx*>& active_links,
		std::size_t depth) const;
	bool apply_diff_to_node(
		mctx& node,
		const mctx& diff,
		bool force_no_diff_forwarding) const;

	static constexpr const char* REF_TAG = "$ref";
	static constexpr const char* ARRAY_TAG = "$as_array";
	static constexpr std::size_t MAX_LINK_DEPTH = 64;

	std::shared_ptr<mctx> _root_context;
	const mctx* _current_node = nullptr;
};

} // namespace dixelu
