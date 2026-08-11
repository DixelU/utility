// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Alexander Verevkin
#include "context_path_detail.h"

#include <utility>

namespace dixelu
{

ContextPath::Query::Query(ContextPath relative_path, std::unique_ptr<mctx> expected):
	_relative_path(std::move(relative_path)),
	_expected(std::move(expected))
{}

ContextPath::Query::Query(const Query& other):
	_relative_path(other._relative_path),
	_expected(other._expected ? std::make_unique<mctx>(*other._expected) : nullptr)
{}

ContextPath::Query::Query(Query&&) noexcept = default;

ContextPath::Query& ContextPath::Query::operator=(const Query& other)
{
	if(this == &other)
		return *this;
	_relative_path = other._relative_path;
	_expected = other._expected ? std::make_unique<mctx>(*other._expected) : nullptr;
	return *this;
}

ContextPath::Query& ContextPath::Query::operator=(Query&&) noexcept = default;
ContextPath::Query::~Query() = default;

ContextPath::ContextPath(): _impl(std::make_unique<Impl>()) {}

ContextPath::ContextPath(std::string key): ContextPath() { append_key(std::move(key)); }

ContextPath::ContextPath(const char* key): ContextPath()
{
	if(key == nullptr)
		throw std::invalid_argument("ContextPath key cannot be null");
	append_key(key);
}

ContextPath::ContextPath(std::size_t index): ContextPath() { append_index(index); }
ContextPath::ContextPath(RootTag): ContextPath() { append_root(); }
ContextPath::ContextPath(VariableTag): ContextPath() { append_variable(); }
ContextPath::ContextPath(PreviousTag): ContextPath() { append_previous(); }

ContextPath::ContextPath(const ContextPath& other):
	_impl(other._impl ? std::make_unique<Impl>(*other._impl) : std::make_unique<Impl>())
{}

ContextPath::ContextPath(ContextPath&&) noexcept = default;

ContextPath& ContextPath::operator=(const ContextPath& other)
{
	if(this != &other)
		_impl = other._impl ? std::make_unique<Impl>(*other._impl) : std::make_unique<Impl>();
	return *this;
}

ContextPath& ContextPath::operator=(ContextPath&&) noexcept = default;
ContextPath::~ContextPath() = default;

std::size_t ContextPath::size() const noexcept { return _impl ? _impl->segments.size() : 0; }
bool ContextPath::empty() const noexcept { return size() == 0; }

void ContextPath::append_key(std::string key)
{
	if(!_impl)
		_impl = std::make_unique<Impl>();
	Impl::Segment segment;
	segment.kind = Impl::Kind::key;
	segment.key = std::move(key);
	_impl->segments.push_back(std::move(segment));
}

void ContextPath::append_index(std::size_t index)
{
	if(!_impl)
		_impl = std::make_unique<Impl>();
	Impl::Segment segment;
	segment.kind = Impl::Kind::index;
	segment.index = index;
	_impl->segments.push_back(std::move(segment));
}

void ContextPath::append_variable()
{
	if(!_impl)
		_impl = std::make_unique<Impl>();
	Impl::Segment segment;
	segment.kind = Impl::Kind::variable;
	_impl->segments.push_back(std::move(segment));
}

void ContextPath::append_previous()
{
	if(!_impl)
		_impl = std::make_unique<Impl>();
	if(!_impl->segments.empty())
	{
		if(_impl->segments.back().kind != Impl::Kind::root)
			_impl->segments.pop_back();
		return;
	}
	Impl::Segment segment;
	segment.kind = Impl::Kind::previous;
	_impl->segments.push_back(std::move(segment));
}

void ContextPath::append_root()
{
	if(!_impl)
		_impl = std::make_unique<Impl>();
	Impl::Segment segment;
	segment.kind = Impl::Kind::root;
	_impl->segments.push_back(std::move(segment));
}

void ContextPath::append_path(const ContextPath& suffix)
{
	if(!suffix._impl)
		return;
	for(const auto& segment : suffix._impl->segments)
	{
		if(segment.kind == Impl::Kind::previous)
			append_previous();
		else
		{
			if(!_impl)
				_impl = std::make_unique<Impl>();
			_impl->segments.push_back(segment);
		}
	}
}

void ContextPath::append_path(ContextPath&& suffix)
{
	if(!suffix._impl)
		return;
	for(auto& segment : suffix._impl->segments)
	{
		if(segment.kind == Impl::Kind::previous)
			append_previous();
		else
		{
			if(!_impl)
				_impl = std::make_unique<Impl>();
			_impl->segments.push_back(std::move(segment));
		}
	}
}

ContextPath operator/(ContextPath path, std::string key)
{
	path.append_key(std::move(key));
	return path;
}

ContextPath operator/(ContextPath path, const char* key)
{
	if(key == nullptr)
		throw std::invalid_argument("ContextPath key cannot be null");
	path.append_key(key);
	return path;
}

ContextPath operator/(ContextPath path, const ContextPath& suffix)
{
	path.append_path(suffix);
	return path;
}

ContextPath operator/(ContextPath path, ContextPath&& suffix)
{
	path.append_path(std::move(suffix));
	return path;
}

ContextPath operator/(ContextPath path, ContextPath::VariableTag)
{
	path.append_variable();
	return path;
}

ContextPath operator/(ContextPath path, ContextPath::PreviousTag)
{
	path.append_previous();
	return path;
}

ContextPath operator/(ContextPath path, ContextPath::RootTag)
{
	path.append_root();
	return path;
}

ContextPath ContextPath::make_predicate(std::function<bool(const mctx&)> predicate)
{
	if(!predicate)
		throw std::invalid_argument("ContextPath predicate cannot be empty");
	ContextPath result;
	Impl::Segment segment;
	segment.kind = Impl::Kind::predicate;
	segment.predicate = std::move(predicate);
	result._impl->segments.push_back(std::move(segment));
	return result;
}

ContextPath::Query ContextPath::queryEquals(ContextPath relative_path, mctx expected)
{
	if(!expected.is_none() && !expected.is<std::string>() && !expected.is<bool>() &&
		!expected.is<std::uint64_t>() && !expected.is<std::int64_t>())
	{
		throw std::invalid_argument(
			"ContextPath equality queries support null, string, bool, and integral values");
	}
	return Query(std::move(relative_path), std::make_unique<mctx>(std::move(expected)));
}

ContextPath::Query ContextPath::queryExists(ContextPath relative_path)
{
	return Query(std::move(relative_path), nullptr);
}

ContextPath ContextPath::whereAll(std::initializer_list<Query> queries)
{
	return whereAll(std::vector<Query>(queries));
}

ContextPath ContextPath::whereAll(std::vector<Query> queries)
{
	if(queries.empty())
		throw std::invalid_argument("ContextPath query set cannot be empty");

	ContextPath result;
	Impl::Segment segment;
	segment.kind = Impl::Kind::queries;
	segment.queries.reserve(queries.size());
	for(auto& query : queries)
	{
		Impl::StoredQuery stored;
		stored.relative_path = std::make_shared<ContextPath>(std::move(query._relative_path));
		if(query._expected)
			stored.expected = std::make_shared<mctx>(std::move(*query._expected));
		segment.queries.push_back(std::move(stored));
	}
	result._impl->segments.push_back(std::move(segment));
	return result;
}

std::string ContextPath::stringify() const
{
	if(!_impl || _impl->segments.empty())
		return {};
	std::string result;
	for(const auto& segment : _impl->segments)
	{
		if(!result.empty())
			result.push_back('/');
		result += _impl->segment_to_string(segment);
	}
	return result;
}

} // namespace dixelu
