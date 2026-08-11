// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Alexander Verevkin
#include "context_path_detail.h"

#include <algorithm>

namespace dixelu
{

mctx& ContextPath::operator[](mctx& target) const
{
	if(!_impl)
		return target;
	mctx* current = &target;
	for(const auto& segment : _impl->segments)
	{
		switch(segment.kind)
		{
			case Impl::Kind::key:
				current = &(*current)[segment.key];
				break;
			case Impl::Kind::index:
				if(!current->is_array() || segment.index >= current->size())
					throw std::out_of_range("ContextPath array index is out of range");
				current = &current->at(segment.index);
				break;
			case Impl::Kind::root:
				if(const auto root_context = _impl->root_context.lock())
					current = root_context.get();
				break;
			case Impl::Kind::previous:
				throw std::logic_error("An unresolved previous segment cannot be traversed");
			case Impl::Kind::variable:
				if(!segment.binding)
					throw std::logic_error("An unbound ContextPath variable is ambiguous");
				if(const auto* key = std::get_if<std::string>(&*segment.binding))
					current = &(*current)[*key];
				else
				{
					const auto index = std::get<std::size_t>(*segment.binding);
					if(!current->is_array() || index >= current->size())
						throw std::out_of_range("ContextPath variable index is out of range");
					current = &current->at(index);
				}
				break;
			case Impl::Kind::predicate:
			case Impl::Kind::queries:
			{
				const auto matches = _impl->all_matches(segment, *current);
				if(matches.empty())
					throw std::out_of_range("ContextPath selector did not match any element");
				current = const_cast<mctx*>(matches.front());
				break;
			}
		}
	}
	return *current;
}

const mctx& ContextPath::operator[](const mctx& target) const
{
	const auto* result = try_get(target);
	if(result == nullptr)
		throw std::out_of_range("ContextPath does not exist or contains an unbound variable");
	return *result;
}

const mctx* ContextPath::try_get(const mctx& target) const
{
	if(!_impl)
		return &target;
	const mctx* current = &target;
	for(const auto& segment : _impl->segments)
	{
		if(segment.kind == Impl::Kind::variable && !segment.binding)
			return nullptr;
		const auto matches = _impl->all_matches(segment, *current);
		if(matches.empty())
			return nullptr;
		current = matches.front();
	}
	return current;
}

bool ContextPath::exists(const mctx& target) const { return try_get(target) != nullptr; }

ContextPath::SmallVector<const mctx*> ContextPath::get_all_matching(const mctx& target) const
{
	SmallVector<const mctx*> current{&target};
	if(!_impl)
		return current;
	for(const auto& segment : _impl->segments)
	{
		SmallVector<const mctx*> next;
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

bool ContextPath::existsAny(const mctx& target) const
{
	return !get_all_matching(target).empty();
}

bool ContextPath::existsAll(const mctx& target) const
{
	return exists_all_recursive(target, 0);
}

bool ContextPath::exists_all_recursive(const mctx& target, std::size_t index) const
{
	if(!_impl || index >= _impl->segments.size())
		return true;
	const auto& segment = _impl->segments[index];
	const auto matches = _impl->all_matches(segment, target);
	const bool branches =
		(segment.kind == Impl::Kind::variable && !segment.binding) ||
		segment.kind == Impl::Kind::predicate ||
		segment.kind == Impl::Kind::queries;
	if(!branches)
		return !matches.empty() && exists_all_recursive(*matches.front(), index + 1);
	return std::ranges::all_of(matches, [&](const mctx* match)
	{
		return exists_all_recursive(*match, index + 1);
	});
}

void ContextPath::set_variable(std::size_t& search_from, std::string key)
{
	if(!_impl)
		throw std::out_of_range("ContextPath has no unbound variable");
	for(; search_from < _impl->segments.size(); ++search_from)
	{
		auto& segment = _impl->segments[search_from];
		if(segment.kind == Impl::Kind::variable && !segment.binding)
		{
			segment.binding = Impl::Binding(std::move(key));
			++search_from;
			return;
		}
	}
	throw std::out_of_range("ContextPath has fewer variables than supplied values");
}

void ContextPath::set_variable(std::size_t& search_from, std::size_t index)
{
	if(!_impl)
		throw std::out_of_range("ContextPath has no unbound variable");
	for(; search_from < _impl->segments.size(); ++search_from)
	{
		auto& segment = _impl->segments[search_from];
		if(segment.kind == Impl::Kind::variable && !segment.binding)
		{
			segment.binding = Impl::Binding(index);
			++search_from;
			return;
		}
	}
	throw std::out_of_range("ContextPath has fewer variables than supplied values");
}

void ContextPath::set_root_context(const std::shared_ptr<mctx>& root_context) const
{
	if(_impl)
		_impl->root_context = root_context;
}

bool ContextPath::remove(mctx& target) const { return remove_recursive(target, 0); }

bool ContextPath::remove_recursive(mctx& target, std::size_t index) const
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

	if(segment.kind == Impl::Kind::root)
	{
		mctx* root_target = &target;
		if(const auto root_context = _impl->root_context.lock())
			root_target = root_context.get();
		if(last)
		{
			root_target->clear();
			return true;
		}
		return remove_recursive(*root_target, index + 1);
	}
	if(segment.kind == Impl::Kind::previous)
		return false;

	if(segment.kind == Impl::Kind::key ||
		(segment.kind == Impl::Kind::variable && segment.binding &&
		 std::holds_alternative<std::string>(*segment.binding)))
	{
		const auto& key = segment.kind == Impl::Kind::key ?
			segment.key : std::get<std::string>(*segment.binding);
		if(last)
			return erase_key(target, key);
		if(!target.is_object())
			return false;
		auto it = target.find(key);
		return it != target.end() && remove_recursive(*it, index + 1);
	}

	if(segment.kind == Impl::Kind::index ||
		(segment.kind == Impl::Kind::variable && segment.binding &&
		 std::holds_alternative<std::size_t>(*segment.binding)))
	{
		const auto element_index = segment.kind == Impl::Kind::index ?
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
