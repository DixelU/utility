// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Alexander Verevkin
#include "linked_mctx_wrapper.h"

#include "mctx_path_serializer.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace dixelu
{

linked_mctx_wrapper::linked_mctx_wrapper(std::shared_ptr<mctx> root_mctx):
	_root_mctx(std::move(root_mctx)),
	_current_node(_root_mctx.get())
{}

linked_mctx_wrapper linked_mctx_wrapper::deep_clone() const
{
	if(!_root_mctx)
		return {};
	return linked_mctx_wrapper(std::make_shared<mctx>(*_root_mctx));
}

linked_mctx_wrapper linked_mctx_wrapper::operator[](std::string key) const
{
	return get_subfield(std::move(key));
}

linked_mctx_wrapper linked_mctx_wrapper::operator[](mctx_path path) const
{
	auto matches = get_by_path(path);
	if(matches.empty())
		return make_shallow_copy();
	return matches.front();
}

linked_mctx_wrapper linked_mctx_wrapper::at(std::string key) const
{
	auto result = operator[](std::move(key));
	if(!result.is_valid())
		throw std::out_of_range("linked_mctx_wrapper key was not found");
	return result;
}

linked_mctx_wrapper linked_mctx_wrapper::at(mctx_path path) const
{
	auto result = operator[](std::move(path));
	if(!result.is_valid())
		throw std::out_of_range("linked_mctx_wrapper path was not found");
	return result;
}

const mctx& linked_mctx_wrapper::get_underlying_mctx_root() const
{
	if(!_root_mctx)
		throw std::logic_error("linked_mctx_wrapper has no root context");
	return *_root_mctx;
}

const mctx& linked_mctx_wrapper::get_underlying_mctx_node() const
{
	if(!_current_node)
		throw std::logic_error("linked_mctx_wrapper has no selected node");
	return *_current_node;
}

linked_mctx_wrapper linked_mctx_wrapper::make_shallow_copy() const
{
	linked_mctx_wrapper copy;
	copy._root_mctx = _root_mctx;
	return copy;
}

linked_mctx_wrapper linked_mctx_wrapper::get_subfield(std::string key) const
{
	auto result = make_shallow_copy();
	if(!_current_node || !_current_node->is_object())
		return result;

	const auto it = _current_node->find(key);
	if(it == _current_node->end())
		return result;

	const mctx* selected = &*it;
	bool is_reference = false;
	const auto* linked = get_linked_mctx(selected, is_reference);
	result._current_node = is_reference ? linked : selected;
	return result;
}

mctx_path::small_vector<linked_mctx_wrapper>
linked_mctx_wrapper::get_by_path(mctx_path& path) const
{
	mctx_path::small_vector<linked_mctx_wrapper> wrappers;
	if(!_current_node)
		return wrappers;

	path.set_root_mctx(_root_mctx);
	for(const auto* node : path.get_all_matching<mctx_path::small_vector>(*_current_node))
	{
		auto wrapper = make_shallow_copy();
		bool is_reference = false;
		const auto* linked = get_linked_mctx(node, is_reference);
		wrapper._current_node = is_reference ? linked : node;
		if(wrapper._current_node)
			wrappers.push_back(std::move(wrapper));
	}
	return wrappers;
}

const mctx* linked_mctx_wrapper::get_linked_mctx(
	const mctx* node_mctx,
	bool& is_reference) const
{
	is_reference = false;
	if(node_mctx == nullptr || !node_mctx->is_object())
		return nullptr;

	const auto reference = node_mctx->find(ref_tag);
	if(reference == node_mctx->end())
		return nullptr;
	is_reference = true;

	mctx_path path;
	if(reference->is<std::string>())
		path = mctx_path_serializer::deserialize(reference->as<std::string>());
	else if(reference->is<mctx_path>())
		path = reference->as<mctx_path>();
	else
		return nullptr;

	path.set_root_mctx(_root_mctx);
	return path.try_get(*node_mctx);
}

mctx_path::small_vector<const mctx*> linked_mctx_wrapper::get_all_linked_mctxs(
	const mctx* node_mctx,
	bool& is_reference) const
{
	is_reference = false;
	if(node_mctx == nullptr || !node_mctx->is_object())
		return {};

	const auto reference = node_mctx->find(ref_tag);
	if(reference == node_mctx->end())
		return {};
	is_reference = true;

	mctx_path path;
	if(reference->is<std::string>())
		path = mctx_path_serializer::deserialize(reference->as<std::string>());
	else if(reference->is<mctx_path>())
		path = reference->as<mctx_path>();
	else
		return {};

	path.set_root_mctx(_root_mctx);
	return path.get_all_matching<mctx_path::small_vector>(*node_mctx);
}

mctx linked_mctx_wrapper::render_mctx_node(
	const mctx* node,
	mctx_path::small_vector<const mctx*>& active_links,
	std::size_t depth) const
{
	if(node == nullptr)
		return {};
	if(depth > max_link_depth)
		throw std::runtime_error("linked_mctx_wrapper link depth exceeds the safety limit");

	bool is_reference = false;
	auto linked_mctxs = get_all_linked_mctxs(node, is_reference);
	if(is_reference)
	{
		if(std::ranges::find(active_links, node) != active_links.end())
			throw std::runtime_error("linked_mctx_wrapper detected a reference cycle");
		active_links.push_back(node);

		mctx result;
		const bool as_array = node->get_as<bool>(array_tag, false);
		if(as_array)
		{
			result = mctx::make_array();
			for(const auto* linked : linked_mctxs)
				result.push_back(render_mctx_node(linked, active_links, depth + 1));
		}
		else if(!linked_mctxs.empty())
			result = render_mctx_node(linked_mctxs.front(), active_links, depth + 1);

		active_links.pop_back();
		return result;
	}

	if(node->is_array())
	{
		mctx result = mctx::make_array();
		for(const auto& child : *node)
			result.push_back(render_mctx_node(&child, active_links, depth));
		return result;
	}

	if(node->is_object())
	{
		mctx result = mctx::make_object();
		for(auto it = node->kvbegin(); it != node->kvend(); ++it)
			result[it->first] = render_mctx_node(&it->second, active_links, depth);
		return result;
	}

	return *node;
}

mctx linked_mctx_wrapper::render_as_linked_mctx() const
{
	mctx_path::small_vector<const mctx*> active_links;
	return render_mctx_node(_current_node, active_links, 0);
}

bool linked_mctx_wrapper::apply_diff_to_node(
	mctx& node,
	const mctx& diff,
	bool force_no_diff_forwarding) const
{
	if(!force_no_diff_forwarding)
	{
		bool is_reference = false;
		auto linked_mctxs = get_all_linked_mctxs(&node, is_reference);
		if(is_reference)
		{
			if(linked_mctxs.empty())
				return false;

			const bool as_array = node.get_as<bool>(array_tag, false);
			if(!as_array)
			{
				auto* linked = const_cast<mctx*>(linked_mctxs.front());
				if(apply_diff_to_node(*linked, diff, false))
					linked->clear();
				return false;
			}

			if(!diff.is_array())
				throw std::invalid_argument(
					"A linked reference array requires an array-shaped rendered diff");
			auto diff_it = diff.begin();
			for(std::size_t index = 0;
				index < linked_mctxs.size() && diff_it != diff.end();
				++index, ++diff_it)
			{
				auto* linked = const_cast<mctx*>(linked_mctxs[index]);
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

void linked_mctx_wrapper::apply_rendered_mctx_diff(
	const mctx& diff,
	bool force_no_diff_forwarding) const
{
	if(!_current_node)
		return;
	auto* mutable_node = const_cast<mctx*>(_current_node);
	if(apply_diff_to_node(*mutable_node, diff, force_no_diff_forwarding))
		mutable_node->clear();
}

linked_mctx_wrapper::trivial_pseudo_iterator::trivial_pseudo_iterator(
	std::shared_ptr<mctx> root_mctx,
	const mctx* owner,
	mctx::value_iter iterator):
	_iterator(std::move(iterator)),
	_root(std::move(root_mctx)),
	_owner(owner)
{}

linked_mctx_wrapper::trivial_pseudo_iterator&
linked_mctx_wrapper::trivial_pseudo_iterator::operator++()
{
	if(!_iterator)
		throw std::out_of_range("Cannot increment an invalid linked_mctx_wrapper iterator");
	++*_iterator;
	return *this;
}

linked_mctx_wrapper::trivial_pseudo_iterator
linked_mctx_wrapper::trivial_pseudo_iterator::operator++(int)
{
	auto copy = *this;
	++*this;
	return copy;
}

linked_mctx_wrapper::trivial_pseudo_iterator&
linked_mctx_wrapper::trivial_pseudo_iterator::operator--()
{
	if(!_iterator)
		throw std::out_of_range("Cannot decrement an invalid linked_mctx_wrapper iterator");
	--*_iterator;
	return *this;
}

linked_mctx_wrapper::trivial_pseudo_iterator
linked_mctx_wrapper::trivial_pseudo_iterator::operator--(int)
{
	auto copy = *this;
	--*this;
	return copy;
}

bool linked_mctx_wrapper::trivial_pseudo_iterator::operator==(
	const trivial_pseudo_iterator& other) const
{
	if(!_iterator || !other._iterator)
		return !_iterator && !other._iterator;
	if(_owner != other._owner)
		return false;
	return *_iterator == *other._iterator;
}

linked_mctx_wrapper linked_mctx_wrapper::trivial_pseudo_iterator::operator*() const
{
	if(!_iterator)
		throw std::out_of_range("Cannot dereference an invalid linked_mctx_wrapper iterator");
	linked_mctx_wrapper wrapper;
	wrapper._root_mctx = _root;
	const mctx* selected = &**_iterator;
	bool is_reference = false;
	const auto* linked = wrapper.get_linked_mctx(selected, is_reference);
	wrapper._current_node = is_reference ? linked : selected;
	return wrapper;
}

linked_mctx_wrapper linked_mctx_wrapper::trivial_pseudo_iterator::operator->() const
{
	return operator*();
}

linked_mctx_wrapper::trivial_pseudo_iterator linked_mctx_wrapper::begin() const
{
	if(!_current_node || (!_current_node->is_array() && !_current_node->is_object()))
		return {};
	return trivial_pseudo_iterator(_root_mctx, _current_node, _current_node->begin());
}

linked_mctx_wrapper::trivial_pseudo_iterator linked_mctx_wrapper::end() const
{
	if(!_current_node || (!_current_node->is_array() && !_current_node->is_object()))
		return {};
	return trivial_pseudo_iterator(_root_mctx, _current_node, _current_node->end());
}

linked_mctx_wrapper::trivial_pseudo_iterator
linked_mctx_wrapper::find(const std::string& key) const
{
	if(!_current_node || !_current_node->is_object())
		return {};
	return trivial_pseudo_iterator(_root_mctx, _current_node, _current_node->find(key));
}

} // namespace dixelu
