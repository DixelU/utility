// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Alexander Verevkin
#include "mctx_path_detail.h"

#include <utility>

namespace dixelu
{

mctx_path::query::query(mctx_path relative_path, std::unique_ptr<mctx> expected):
	_relative_path(std::move(relative_path)),
	_expected(std::move(expected))
{}

mctx_path::query::query(const query& other):
	_relative_path(other._relative_path),
	_expected(other._expected ? std::make_unique<mctx>(*other._expected) : nullptr)
{}

mctx_path::query::query(query&&) noexcept = default;

mctx_path::query& mctx_path::query::operator=(const query& other)
{
	if(this == &other)
		return *this;
	_relative_path = other._relative_path;
	_expected = other._expected ? std::make_unique<mctx>(*other._expected) : nullptr;
	return *this;
}

mctx_path::query& mctx_path::query::operator=(query&&) noexcept = default;
mctx_path::query::~query() = default;

mctx_path::mctx_path(): _impl(std::make_unique<impl>()) {}

mctx_path::mctx_path(std::string key): mctx_path() { append_key(std::move(key)); }

mctx_path::mctx_path(const char* key): mctx_path()
{
	if(key == nullptr)
		throw std::invalid_argument("mctx_path key cannot be null");
	append_key(key);
}

mctx_path::mctx_path(std::size_t index): mctx_path() { append_index(index); }
mctx_path::mctx_path(root_tag): mctx_path() { append_root(); }
mctx_path::mctx_path(variable_tag): mctx_path() { append_variable(); }
mctx_path::mctx_path(previous_tag): mctx_path() { append_previous(); }

mctx_path::mctx_path(const mctx_path& other):
	_impl(other._impl ? std::make_unique<impl>(*other._impl) : std::make_unique<impl>())
{}

mctx_path::mctx_path(mctx_path&&) noexcept = default;

mctx_path& mctx_path::operator=(const mctx_path& other)
{
	if(this != &other)
		_impl = other._impl ? std::make_unique<impl>(*other._impl) : std::make_unique<impl>();
	return *this;
}

mctx_path& mctx_path::operator=(mctx_path&&) noexcept = default;
mctx_path::~mctx_path() = default;

std::size_t mctx_path::size() const noexcept { return _impl ? _impl->segments.size() : 0; }
bool mctx_path::empty() const noexcept { return size() == 0; }

void mctx_path::append_key(std::string key)
{
	if(!_impl)
		_impl = std::make_unique<impl>();
	impl::segment segment;
	segment.kind = impl::kind::key;
	segment.key = std::move(key);
	_impl->segments.push_back(std::move(segment));
}

void mctx_path::append_index(std::size_t index)
{
	if(!_impl)
		_impl = std::make_unique<impl>();
	impl::segment segment;
	segment.kind = impl::kind::index;
	segment.index = index;
	_impl->segments.push_back(std::move(segment));
}

void mctx_path::append_variable()
{
	if(!_impl)
		_impl = std::make_unique<impl>();
	impl::segment segment;
	segment.kind = impl::kind::variable;
	_impl->segments.push_back(std::move(segment));
}

void mctx_path::append_previous()
{
	if(!_impl)
		_impl = std::make_unique<impl>();
	if(!_impl->segments.empty())
	{
		if(_impl->segments.back().kind != impl::kind::root)
			_impl->segments.pop_back();
		return;
	}
	impl::segment segment;
	segment.kind = impl::kind::previous;
	_impl->segments.push_back(std::move(segment));
}

void mctx_path::append_root()
{
	if(!_impl)
		_impl = std::make_unique<impl>();
	impl::segment segment;
	segment.kind = impl::kind::root;
	_impl->segments.push_back(std::move(segment));
}

void mctx_path::append_path(const mctx_path& suffix)
{
	if(!suffix._impl)
		return;
	for(const auto& segment : suffix._impl->segments)
	{
		if(segment.kind == impl::kind::previous)
			append_previous();
		else
		{
			if(!_impl)
				_impl = std::make_unique<impl>();
			_impl->segments.push_back(segment);
		}
	}
}

void mctx_path::append_path(mctx_path&& suffix)
{
	if(!suffix._impl)
		return;
	for(auto& segment : suffix._impl->segments)
	{
		if(segment.kind == impl::kind::previous)
			append_previous();
		else
		{
			if(!_impl)
				_impl = std::make_unique<impl>();
			_impl->segments.push_back(std::move(segment));
		}
	}
}

mctx_path operator/(mctx_path path, std::string key)
{
	path.append_key(std::move(key));
	return path;
}

mctx_path operator/(mctx_path path, const char* key)
{
	if(key == nullptr)
		throw std::invalid_argument("mctx_path key cannot be null");
	path.append_key(key);
	return path;
}

mctx_path operator/(mctx_path path, const mctx_path& suffix)
{
	path.append_path(suffix);
	return path;
}

mctx_path operator/(mctx_path path, mctx_path&& suffix)
{
	path.append_path(std::move(suffix));
	return path;
}

mctx_path operator/(mctx_path path, mctx_path::variable_tag)
{
	path.append_variable();
	return path;
}

mctx_path operator/(mctx_path path, mctx_path::previous_tag)
{
	path.append_previous();
	return path;
}

mctx_path operator/(mctx_path path, mctx_path::root_tag)
{
	path.append_root();
	return path;
}

mctx_path mctx_path::make_predicate(std::function<bool(const mctx&)> predicate)
{
	if(!predicate)
		throw std::invalid_argument("mctx_path predicate cannot be empty");
	mctx_path result;
	impl::segment segment;
	segment.kind = impl::kind::predicate;
	segment.predicate = std::move(predicate);
	result._impl->segments.push_back(std::move(segment));
	return result;
}

mctx_path::query mctx_path::query_equals(mctx_path relative_path, mctx expected)
{
	if(!expected.is_none() && !expected.is<std::string>() && !expected.is<bool>() &&
		!expected.is<std::uint64_t>() && !expected.is<std::int64_t>())
	{
		throw std::invalid_argument(
			"mctx_path equality queries support null, string, bool, and integral values");
	}
	return query(std::move(relative_path), std::make_unique<mctx>(std::move(expected)));
}

mctx_path::query mctx_path::query_exists(mctx_path relative_path)
{
	return query(std::move(relative_path), nullptr);
}

mctx_path mctx_path::where_all(std::initializer_list<query> queries)
{
	return where_all(std::vector<query>(queries));
}

mctx_path mctx_path::where_all(std::vector<query> queries)
{
	if(queries.empty())
		throw std::invalid_argument("mctx_path query set cannot be empty");

	mctx_path result;
	impl::segment segment;
	segment.kind = impl::kind::queries;
	segment.queries.reserve(queries.size());
	for(auto& query : queries)
	{
		impl::Storedquery stored;
		stored.relative_path = std::make_shared<mctx_path>(std::move(query._relative_path));
		if(query._expected)
			stored.expected = std::make_shared<mctx>(std::move(*query._expected));
		segment.queries.push_back(std::move(stored));
	}
	result._impl->segments.push_back(std::move(segment));
	return result;
}

std::string mctx_path::stringify() const
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
