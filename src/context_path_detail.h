// Internal implementation shared by the ContextPath source chunks.
#pragma once

#include "context_path.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <variant>

namespace dixelu
{

struct ContextPath::Impl
{
	enum class Kind
	{
		key,
		index,
		root,
		previous,
		variable,
		predicate,
		queries
	};

	using Binding = std::variant<std::string, std::size_t>;

	struct StoredQuery
	{
		std::shared_ptr<ContextPath> relative_path;
		std::shared_ptr<mctx> expected;
	};

	struct Segment
	{
		Kind kind = Kind::variable;
		std::string key;
		std::size_t index = 0;
		std::optional<Binding> binding;
		std::function<bool(const mctx&)> predicate;
		std::vector<StoredQuery> queries;
	};

	std::deque<Segment> segments;
	mutable std::weak_ptr<mctx> root_context;

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

	[[nodiscard]] bool query_matches(const StoredQuery& query, const mctx& child) const
	{
		const auto matches = query.relative_path->get_all_matching(child);
		if(query.expected == nullptr)
			return !matches.empty();
		return std::ranges::any_of(matches, [&](const mctx* match)
		{
			return scalar_equals(*match, *query.expected);
		});
	}

	[[nodiscard]] bool segment_matches_child(const Segment& segment, const mctx& child) const
	{
		switch(segment.kind)
		{
			case Kind::variable: return !segment.binding.has_value();
			case Kind::predicate: return segment.predicate(child);
			case Kind::queries:
				return std::ranges::all_of(segment.queries, [&](const StoredQuery& query)
				{
					return query_matches(query, child);
				});
			default: return false;
		}
	}

	[[nodiscard]] SmallVector<const mctx*> all_matches(
		const Segment& segment,
		const mctx& current) const
	{
		SmallVector<const mctx*> result;
		switch(segment.kind)
		{
			case Kind::key:
			{
				if(!current.is_object())
					return result;
				const auto it = current.find(segment.key);
				if(it != current.end())
					result.push_back(&*it);
				return result;
			}
			case Kind::index:
				if(current.is_array() && segment.index < current.size())
					result.push_back(&current.at(segment.index));
				return result;
			case Kind::root:
			{
				const auto locked_root = root_context.lock();
				result.push_back(locked_root ? locked_root.get() : &current);
				return result;
			}
			case Kind::previous:
				return result;
			case Kind::variable:
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
			case Kind::predicate:
			case Kind::queries:
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
		throw std::logic_error("ContextPath query contains a non-serializable value");
	}

	[[nodiscard]] std::string segment_to_string(const Segment& segment) const
	{
		switch(segment.kind)
		{
			case Kind::key: return quote(segment.key);
			case Kind::index: return std::to_string(segment.index);
			case Kind::root: return "#";
			case Kind::previous: return "..";
			case Kind::variable: return "*";
			case Kind::predicate: return "*:{??predicate??}";
			case Kind::queries:
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
		throw std::logic_error("Unknown ContextPath segment");
	}
};

} // namespace dixelu
