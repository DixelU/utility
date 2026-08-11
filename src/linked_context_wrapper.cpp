// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Alexander Verevkin
#include "linked_context_wrapper.h"

#include "context_path_serializer.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace dixelu
{

LinkedContextWrapper::LinkedContextWrapper(std::shared_ptr<mctx> root_context):
	_root_context(std::move(root_context)),
	_current_node(_root_context.get())
{}

LinkedContextWrapper LinkedContextWrapper::deepClone() const
{
	if(!_root_context)
		return {};
	return LinkedContextWrapper(std::make_shared<mctx>(*_root_context));
}

LinkedContextWrapper LinkedContextWrapper::operator[](std::string key) const
{
	return get_subfield(std::move(key));
}

LinkedContextWrapper LinkedContextWrapper::operator[](ContextPath path) const
{
	auto matches = get_by_path(path);
	if(matches.empty())
		return make_shallow_copy();
	return matches.front();
}

LinkedContextWrapper LinkedContextWrapper::at(std::string key) const
{
	auto result = operator[](std::move(key));
	if(!result.isValid())
		throw std::out_of_range("LinkedContextWrapper key was not found");
	return result;
}

LinkedContextWrapper LinkedContextWrapper::at(ContextPath path) const
{
	auto result = operator[](std::move(path));
	if(!result.isValid())
		throw std::out_of_range("LinkedContextWrapper path was not found");
	return result;
}

const mctx& LinkedContextWrapper::getUnderlyingContextRoot() const
{
	if(!_root_context)
		throw std::logic_error("LinkedContextWrapper has no root context");
	return *_root_context;
}

const mctx& LinkedContextWrapper::getUnderlyingContextNode() const
{
	if(!_current_node)
		throw std::logic_error("LinkedContextWrapper has no selected node");
	return *_current_node;
}

LinkedContextWrapper LinkedContextWrapper::make_shallow_copy() const
{
	LinkedContextWrapper copy;
	copy._root_context = _root_context;
	return copy;
}

LinkedContextWrapper LinkedContextWrapper::get_subfield(std::string key) const
{
	auto result = make_shallow_copy();
	if(!_current_node || !_current_node->is_object())
		return result;

	const auto it = _current_node->find(key);
	if(it == _current_node->end())
		return result;

	const mctx* selected = &*it;
	bool is_reference = false;
	const auto* linked = get_linked_context(selected, is_reference);
	result._current_node = is_reference ? linked : selected;
	return result;
}

ContextPath::SmallVector<LinkedContextWrapper>
LinkedContextWrapper::get_by_path(ContextPath& path) const
{
	ContextPath::SmallVector<LinkedContextWrapper> wrappers;
	if(!_current_node)
		return wrappers;

	path.set_root_context(_root_context);
	for(const auto* node : path.getAllMatching<ContextPath::SmallVector>(*_current_node))
	{
		auto wrapper = make_shallow_copy();
		bool is_reference = false;
		const auto* linked = get_linked_context(node, is_reference);
		wrapper._current_node = is_reference ? linked : node;
		if(wrapper._current_node)
			wrappers.push_back(std::move(wrapper));
	}
	return wrappers;
}

const mctx* LinkedContextWrapper::get_linked_context(
	const mctx* node_context,
	bool& is_reference) const
{
	is_reference = false;
	if(node_context == nullptr || !node_context->is_object())
		return nullptr;

	const auto reference = node_context->find(REF_TAG);
	if(reference == node_context->end())
		return nullptr;
	is_reference = true;

	ContextPath path;
	if(reference->is<std::string>())
		path = ContextPathSerializer::deserialize(reference->as<std::string>());
	else if(reference->is<ContextPath>())
		path = reference->as<ContextPath>();
	else
		return nullptr;

	path.set_root_context(_root_context);
	return path.try_get(*node_context);
}

ContextPath::SmallVector<const mctx*> LinkedContextWrapper::get_all_linked_contexts(
	const mctx* node_context,
	bool& is_reference) const
{
	is_reference = false;
	if(node_context == nullptr || !node_context->is_object())
		return {};

	const auto reference = node_context->find(REF_TAG);
	if(reference == node_context->end())
		return {};
	is_reference = true;

	ContextPath path;
	if(reference->is<std::string>())
		path = ContextPathSerializer::deserialize(reference->as<std::string>());
	else if(reference->is<ContextPath>())
		path = reference->as<ContextPath>();
	else
		return {};

	path.set_root_context(_root_context);
	return path.getAllMatching<ContextPath::SmallVector>(*node_context);
}

mctx LinkedContextWrapper::render_node(
	const mctx* node,
	ContextPath::SmallVector<const mctx*>& active_links,
	std::size_t depth) const
{
	if(node == nullptr)
		return {};
	if(depth > MAX_LINK_DEPTH)
		throw std::runtime_error("LinkedContextWrapper link depth exceeds the safety limit");

	bool is_reference = false;
	auto linked_contexts = get_all_linked_contexts(node, is_reference);
	if(is_reference)
	{
		if(std::ranges::find(active_links, node) != active_links.end())
			throw std::runtime_error("LinkedContextWrapper detected a reference cycle");
		active_links.push_back(node);

		mctx result;
		const bool as_array = node->get_as<bool>(ARRAY_TAG, false);
		if(as_array)
		{
			result = mctx::make_array();
			for(const auto* linked : linked_contexts)
				result.push_back(render_node(linked, active_links, depth + 1));
		}
		else if(!linked_contexts.empty())
			result = render_node(linked_contexts.front(), active_links, depth + 1);

		active_links.pop_back();
		return result;
	}

	if(node->is_array())
	{
		mctx result = mctx::make_array();
		for(const auto& child : *node)
			result.push_back(render_node(&child, active_links, depth));
		return result;
	}

	if(node->is_object())
	{
		mctx result = mctx::make_object();
		for(auto it = node->kvbegin(); it != node->kvend(); ++it)
			result[it->first] = render_node(&it->second, active_links, depth);
		return result;
	}

	return *node;
}

mctx LinkedContextWrapper::renderAsLinkedContext() const
{
	ContextPath::SmallVector<const mctx*> active_links;
	return render_node(_current_node, active_links, 0);
}

bool LinkedContextWrapper::apply_diff_to_node(
	mctx& node,
	const mctx& diff,
	bool force_no_diff_forwarding) const
{
	if(!force_no_diff_forwarding)
	{
		bool is_reference = false;
		auto linked_contexts = get_all_linked_contexts(&node, is_reference);
		if(is_reference)
		{
			if(linked_contexts.empty())
				return false;

			const bool as_array = node.get_as<bool>(ARRAY_TAG, false);
			if(!as_array)
			{
				auto* linked = const_cast<mctx*>(linked_contexts.front());
				if(apply_diff_to_node(*linked, diff, false))
					linked->clear();
				return false;
			}

			if(!diff.is_array())
				throw std::invalid_argument(
					"A linked reference array requires an array-shaped rendered diff");
			auto diff_it = diff.begin();
			for(std::size_t index = 0;
				index < linked_contexts.size() && diff_it != diff.end();
				++index, ++diff_it)
			{
				auto* linked = const_cast<mctx*>(linked_contexts[index]);
				if(apply_diff_to_node(*linked, *diff_it, false))
					linked->clear();
			}
			return false;
		}
	}

	if(diff.is_none())
		return false;

	if(diff.is_object())
	{
		const auto addition = diff.find("+");
		const auto deletion = diff.find("-");
		const bool has_addition = addition != diff.end();
		const bool has_deletion = deletion != diff.end();
		if(has_addition || has_deletion)
		{
			if(has_addition)
			{
				node = *addition;
				return false;
			}
			return true;
		}

		if(!node.is_object())
			node = mctx::make_object();
		for(auto it = diff.kvbegin(); it != diff.kvend(); ++it)
		{
			auto current = node.find(it->first);
			if(current == node.end())
			{
				node[it->first];
				current = node.find(it->first);
			}
			if(apply_diff_to_node(*current, it->second, force_no_diff_forwarding))
				node.erase(it->first);
		}
		return false;
	}

	if(diff.is_array())
	{
		if(!node.is_array())
			node = mctx::make_array();

		std::size_t node_index = 0;
		for(auto diff_it = diff.begin(); diff_it != diff.end(); ++diff_it)
		{
			while(node_index >= node.size())
				node.push_back(mctx{});
			if(apply_diff_to_node(node.at(node_index), *diff_it, force_no_diff_forwarding))
			{
				auto erase_it = node.begin();
				for(std::size_t index = 0; index < node_index; ++index)
					++erase_it;
				node.erase(erase_it);
			}
			else
				++node_index;
		}
		return false;
	}

	node = diff;
	return false;
}

void LinkedContextWrapper::applyRenderedContextDiff(
	const mctx& diff,
	bool force_no_diff_forwarding) const
{
	if(!_current_node)
		return;
	auto* mutable_node = const_cast<mctx*>(_current_node);
	if(apply_diff_to_node(*mutable_node, diff, force_no_diff_forwarding))
		mutable_node->clear();
}

LinkedContextWrapper::TrivialPseudoIterator::TrivialPseudoIterator(
	std::shared_ptr<mctx> root_context,
	const mctx* owner,
	mctx::value_iter iterator):
	_iterator(std::move(iterator)),
	_root(std::move(root_context)),
	_owner(owner)
{}

LinkedContextWrapper::TrivialPseudoIterator&
LinkedContextWrapper::TrivialPseudoIterator::operator++()
{
	if(!_iterator)
		throw std::out_of_range("Cannot increment an invalid LinkedContextWrapper iterator");
	++*_iterator;
	return *this;
}

LinkedContextWrapper::TrivialPseudoIterator
LinkedContextWrapper::TrivialPseudoIterator::operator++(int)
{
	auto copy = *this;
	++*this;
	return copy;
}

LinkedContextWrapper::TrivialPseudoIterator&
LinkedContextWrapper::TrivialPseudoIterator::operator--()
{
	if(!_iterator)
		throw std::out_of_range("Cannot decrement an invalid LinkedContextWrapper iterator");
	--*_iterator;
	return *this;
}

LinkedContextWrapper::TrivialPseudoIterator
LinkedContextWrapper::TrivialPseudoIterator::operator--(int)
{
	auto copy = *this;
	--*this;
	return copy;
}

bool LinkedContextWrapper::TrivialPseudoIterator::operator==(
	const TrivialPseudoIterator& other) const
{
	if(!_iterator || !other._iterator)
		return !_iterator && !other._iterator;
	if(_owner != other._owner)
		return false;
	return *_iterator == *other._iterator;
}

LinkedContextWrapper LinkedContextWrapper::TrivialPseudoIterator::operator*() const
{
	if(!_iterator)
		throw std::out_of_range("Cannot dereference an invalid LinkedContextWrapper iterator");
	LinkedContextWrapper wrapper;
	wrapper._root_context = _root;
	const mctx* selected = &**_iterator;
	bool is_reference = false;
	const auto* linked = wrapper.get_linked_context(selected, is_reference);
	wrapper._current_node = is_reference ? linked : selected;
	return wrapper;
}

LinkedContextWrapper LinkedContextWrapper::TrivialPseudoIterator::operator->() const
{
	return operator*();
}

LinkedContextWrapper::TrivialPseudoIterator LinkedContextWrapper::begin() const
{
	if(!_current_node || (!_current_node->is_array() && !_current_node->is_object()))
		return {};
	return TrivialPseudoIterator(_root_context, _current_node, _current_node->begin());
}

LinkedContextWrapper::TrivialPseudoIterator LinkedContextWrapper::end() const
{
	if(!_current_node || (!_current_node->is_array() && !_current_node->is_object()))
		return {};
	return TrivialPseudoIterator(_root_context, _current_node, _current_node->end());
}

LinkedContextWrapper::TrivialPseudoIterator
LinkedContextWrapper::find(const std::string& key) const
{
	if(!_current_node || !_current_node->is_object())
		return {};
	return TrivialPseudoIterator(_root_context, _current_node, _current_node->find(key));
}

} // namespace dixelu
