// Internal implementation shared by the mctx_path source chunks.
#pragma once

#include "mctx_path.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <variant>

namespace dixelu
{

struct mctx_path::impl
{
	enum class kind
	{
		key,
		index,
		root,
		previous,
		variable,
		predicate,
		queries
	};

	using binding = std::variant<std::string, std::size_t>;

	struct Storedquery
	{
		std::shared_ptr<mctx_path> relative_path;
		std::shared_ptr<mctx> expected;
	};

	struct segment
	{
		kind kind = kind::variable;
		std::string key;
		std::size_t index = 0;
		std::optional<binding> binding;
		std::function<bool(const mctx&)> predicate;
		std::vector<Storedquery> queries;
	};

	std::deque<segment> segments;
	mutable std::weak_ptr<mctx> root_mctx;

	[[nodiscard]] static bool scalar_equals(const mctx& lhs, const mctx& rhs)
	{
		const bool lhs_unsigned = lhs.is<std::uint64_t>();
		const bool rhs_unsigned = rhs.is<std::uint64_t>();
		const bool lhs_integral = lhs_unsigned || lhs.is<std::int64_t>();
		const bool rhs_integral = rhs_unsigned || rhs.is<std::int64_t>();

		if(lhs_integral && rhs_integral)
		{
			if(lhs_unsigned && rhs_unsigned)
				return lhs.get<std::uint64_t>() == rhs.get<std::uint64_t>();
			if(!lhs_unsigned && !rhs_unsigned)
				return lhs.get<std::int64_t>() == rhs.get<std::int64_t>();

			const auto& unsigned_value = lhs_unsigned ? lhs : rhs;
			const auto& signed_value = lhs_unsigned ? rhs : lhs;
			const auto signed_number = signed_value.get<std::int64_t>();
			return signed_number >= 0 &&
				unsigned_value.get<std::uint64_t>() == static_cast<std::uint64_t>(signed_number);
		}
		return lhs == rhs;
	}

	[[nodiscard]] bool query_matches(const Storedquery& query, const mctx& child) const
	{
		const auto matches = query.relative_path->get_all_matches(child);
		if(query.expected == nullptr)
			return !matches.empty();
		return std::ranges::any_of(matches, [&](const mctx* match)
		{
			return scalar_equals(*match, *query.expected);
		});
	}

	[[nodiscard]] bool segment_matches_child(const segment& segment, const mctx& child) const
	{
		switch(segment.kind)
		{
			case kind::variable: return !segment.binding.has_value();
			case kind::predicate: return segment.predicate(child);
			case kind::queries:
				return std::ranges::all_of(segment.queries, [&](const Storedquery& query)
				{
					return query_matches(query, child);
				});
			default: return false;
		}
	}

	[[nodiscard]] small_vector<const mctx*> all_matches(
		const segment& segment,
		const mctx& current) const
	{
		small_vector<const mctx*> result;
		switch(segment.kind)
		{
			case kind::key:
			{
				if(!current.is_object())
					return result;
				const auto it = current.find(segment.key);
				if(it != current.end())
					result.push_back(&*it);
				return result;
			}
			case kind::index:
				if(current.is_array() && segment.index < current.size())
					result.push_back(&current.at(segment.index));
				return result;
			case kind::root:
			{
				const auto locked_root = root_mctx.lock();
				result.push_back(locked_root ? locked_root.get() : &current);
				return result;
			}
			case kind::previous:
				return result;
			case kind::variable:
			{
				if(segment.binding)
				{
					if(const auto* key = std::get_if<std::string>(&*segment.binding))
					{
						if(!current.is_object())
							return result;
						const auto it = current.find(*key);
						if(it != current.end())
							result.push_back(&*it);
					}
					else
					{
						const auto index = std::get<std::size_t>(*segment.binding);
						if(current.is_array() && index < current.size())
							result.push_back(&current.at(index));
					}
					return result;
				}
				break;
			}
			case kind::predicate:
			case kind::queries:
				break;
		}

		if(!current.is_array() && !current.is_object())
			return result;
		for(const auto& child : current)
			if(segment_matches_child(segment, child))
				result.push_back(&child);
		return result;
	}

	[[nodiscard]] static std::string quote(std::string_view value)
	{
		std::string result;
		result.reserve(value.size() + 2);
		result.push_back('"');
		for(const char c : value)
		{
			switch(c)
			{
				case '\\': result += "\\\\"; break;
				case '"': result += "\\\""; break;
				case '\n': result += "\\n"; break;
				case '\r': result += "\\r"; break;
				case '\t': result += "\\t"; break;
				default: result.push_back(c); break;
			}
		}
		result.push_back('"');
		return result;
	}

	[[nodiscard]] static std::string expected_to_string(const mctx& expected)
	{
		if(expected.is_none())
			return "null";
		if(expected.is<std::string>())
			return quote(expected.as<std::string>());
		if(expected.is<bool>())
			return expected.get<bool>() ? "true" : "false";
		if(expected.is<std::uint64_t>())
			return std::to_string(expected.get<std::uint64_t>());
		if(expected.is<std::int64_t>())
			return std::to_string(expected.get<std::int64_t>());
		throw std::logic_error("mctx_path query contains a non-serializable value");
	}

	[[nodiscard]] std::string segment_to_string(const segment& segment) const
	{
		switch(segment.kind)
		{
			case kind::key: return quote(segment.key);
			case kind::index: return std::to_string(segment.index);
			case kind::root: return "#";
			case kind::previous: return "..";
			case kind::variable: return "*";
			case kind::predicate: return "*:{??predicate??}";
			case kind::queries:
			{
				std::string result = "*:{";
				for(std::size_t index = 0; index < segment.queries.size(); ++index)
				{
					if(index != 0)
						result.push_back('&');
					const auto& query = segment.queries[index];
					result += "*/";
					result += query.relative_path->stringify();
					result.push_back('=');
					result += query.expected ? expected_to_string(*query.expected) : "*";
				}
				result.push_back('}');
				return result;
			}
		}
		throw std::logic_error("Unknown mctx_path segment");
	}
};

} // namespace dixelu
