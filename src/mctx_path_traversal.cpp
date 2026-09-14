// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Alexander Verevkin
#include "mctx_path_detail.h"

#include <algorithm>

namespace dixelu
{

mctx& mctx_path::operator[](mctx& target) const
{
	if(!_impl)
		return target;
	mctx* current = &target;
	for(const auto& segment : _impl->segments)
	{
		switch(segment.kind)
		{
			case impl::kind::key:
				current = &(*current)[segment.key];
				break;
			case impl::kind::index:
				if(!current->is_array() || segment.index >= current->size())
					throw std::out_of_range("mctx_path array index is out of range");
				current = &current->at(segment.index);
				break;
			case impl::kind::root:
				if(const auto root_mctx = _impl->root_mctx.lock())
					current = root_mctx.get();
				break;
			case impl::kind::previous:
				throw std::logic_error("An unresolved previous segment cannot be traversed");
			case impl::kind::variable:
				if(!segment.binding)
					throw std::logic_error("An unbound mctx_path variable is ambiguous");
				if(const auto* key = std::get_if<std::string>(&*segment.binding))
					current = &(*current)[*key];
				else
				{
					const auto index = std::get<std::size_t>(*segment.binding);
					if(!current->is_array() || index >= current->size())
						throw std::out_of_range("mctx_path variable index is out of range");
					current = &current->at(index);
				}
				break;
			case impl::kind::predicate:
			case impl::kind::queries:
			{
				const auto matches = _impl->all_matches(segment, *current);
				if(matches.empty())
					throw std::out_of_range("mctx_path selector did not match any element");
				current = const_cast<mctx*>(matches.front());
				break;
			}
		}
	}
	return *current;
}

const mctx& mctx_path::operator[](const mctx& target) const
{
	const auto* result = try_get(target);
	if(result == nullptr)
		throw std::out_of_range("mctx_path does not exist or contains an unbound variable");
	return *result;
}

const mctx* mctx_path::try_get(const mctx& target) const
{
	if(!_impl)
		return &target;
	const mctx* current = &target;
	for(const auto& segment : _impl->segments)
	{
		if(segment.kind == impl::kind::variable && !segment.binding)
			return nullptr;
		const auto matches = _impl->all_matches(segment, *current);
		if(matches.empty())
			return nullptr;
		current = matches.front();
	}
	return current;
}

bool mctx_path::exists(const mctx& target) const { return try_get(target) != nullptr; }

mctx_path::small_vector<const mctx*> mctx_path::get_all_matches(const mctx& target) const
{
	small_vector<const mctx*> current{&target};
	if(!_impl)
		return current;
	for(const auto& segment : _impl->segments)
	{
		small_vector<const mctx*> next;
		for(const auto* node : current)
		{
			auto matches = _impl->all_matches(segment, *node);
			next.insert(next.end(), matches.begin(), matches.end());
		}
		current = std::move(next);
		if(current.empty())
			break;
	}
	return current;
}

bool mctx_path::exists_any(const mctx& target) const
{
	return !get_all_matches(target).empty();
}

bool mctx_path::exists_all(const mctx& target) const
{
	return exists_all_recursive(target, 0);
}

bool mctx_path::exists_all_recursive(const mctx& target, std::size_t index) const
{
	if(!_impl || index >= _impl->segments.size())
		return true;
	const auto& segment = _impl->segments[index];
	const auto matches = _impl->all_matches(segment, target);
	const bool branches =
		(segment.kind == impl::kind::variable && !segment.binding) ||
		segment.kind == impl::kind::predicate ||
		segment.kind == impl::kind::queries;
	if(!branches)
		return !matches.empty() && exists_all_recursive(*matches.front(), index + 1);
	return std::ranges::all_of(matches, [&](const mctx* match)
	{
		return exists_all_recursive(*match, index + 1);
	});
}

void mctx_path::set_variable(std::size_t& search_from, std::string key)
{
	if(!_impl)
		throw std::out_of_range("mctx_path has no unbound variable");
	for(; search_from < _impl->segments.size(); ++search_from)
	{
		auto& segment = _impl->segments[search_from];
		if(segment.kind == impl::kind::variable && !segment.binding)
		{
			segment.binding = impl::binding(std::move(key));
			++search_from;
			return;
		}
	}
	throw std::out_of_range("mctx_path has fewer variables than supplied values");
}

void mctx_path::set_variable(std::size_t& search_from, std::size_t index)
{
	if(!_impl)
		throw std::out_of_range("mctx_path has no unbound variable");
	for(; search_from < _impl->segments.size(); ++search_from)
	{
		auto& segment = _impl->segments[search_from];
		if(segment.kind == impl::kind::variable && !segment.binding)
		{
			segment.binding = impl::binding(index);
			++search_from;
			return;
		}
	}
	throw std::out_of_range("mctx_path has fewer variables than supplied values");
}

void mctx_path::set_root_mctx(const std::shared_ptr<mctx>& root_mctx) const
{
	if(_impl)
		_impl->root_mctx = root_mctx;
}

bool mctx_path::remove(mctx& target) const { return remove_recursive(target, 0); }

bool mctx_path::remove_recursive(mctx& target, std::size_t index) const
{
	if(!_impl || index >= _impl->segments.size())
		return false;
	const auto& segment = _impl->segments[index];
	const bool last = index + 1 == _impl->segments.size();

	auto erase_index = [](mctx& parent, std::size_t element_index)
	{
		if(!parent.is_array() || element_index >= parent.size())
			return false;
		auto it = parent.begin();
		for(std::size_t current = 0; current < element_index; ++current)
			++it;
		parent.erase(it);
		return true;
	};

	auto erase_key = [](mctx& parent, const std::string& key)
	{
		if(!parent.is_object())
			return false;
		auto it = parent.find(key);
		if(it == parent.end())
			return false;
		parent.erase(it);
		return true;
	};

	if(segment.kind == impl::kind::root)
	{
		mctx* root_target = &target;
		if(const auto root_mctx = _impl->root_mctx.lock())
			root_target = root_mctx.get();
		if(last)
		{
			root_target->clear();
			return true;
		}
		return remove_recursive(*root_target, index + 1);
	}
	if(segment.kind == impl::kind::previous)
		return false;

	if(segment.kind == impl::kind::key ||
		(segment.kind == impl::kind::variable && segment.binding &&
		 std::holds_alternative<std::string>(*segment.binding)))
	{
		const auto& key = segment.kind == impl::kind::key ?
			segment.key : std::get<std::string>(*segment.binding);
		if(last)
			return erase_key(target, key);
		if(!target.is_object())
			return false;
		auto it = target.find(key);
		return it != target.end() && remove_recursive(*it, index + 1);
	}

	if(segment.kind == impl::kind::index ||
		(segment.kind == impl::kind::variable && segment.binding &&
		 std::holds_alternative<std::size_t>(*segment.binding)))
	{
		const auto element_index = segment.kind == impl::kind::index ?
			segment.index : std::get<std::size_t>(*segment.binding);
		if(last)
			return erase_index(target, element_index);
		if(!target.is_array() || element_index >= target.size())
			return false;
		return remove_recursive(target.at(element_index), index + 1);
	}

	if(!target.is_array() && !target.is_object())
		return false;
	if(last)
	{
		bool removed = false;
		for(auto it = target.begin(); it != target.end();)
		{
			if(_impl->segment_matches_child(segment, *it))
			{
				it = target.erase(it);
				removed = true;
			}
			else
				++it;
		}
		return removed;
	}

	bool removed = false;
	for(auto it = target.begin(); it != target.end(); ++it)
		if(_impl->segment_matches_child(segment, *it))
			removed = remove_recursive(*it, index + 1) || removed;
	return removed;
}

} // namespace dixelu
