#include "ContextPath.h"

#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{

using dixelu::mctx;

void require(bool condition, std::string_view message)
{
	if(!condition)
		throw std::runtime_error(std::string(message));
}

template<typename Exception = std::exception, typename Function>
void require_throws(Function&& function, std::string_view message)
{
	try
	{
		std::invoke(std::forward<Function>(function));
	}
	catch(const Exception&)
	{
		return;
	}
	throw std::runtime_error(std::string(message));
}

mctx make_user(std::uint64_t id, std::string name, std::string role, bool active)
{
	mctx user;
	user["id"] = id;
	user["name"] = std::move(name);
	user["role"] = std::move(role);
	user["active"] = active;
	return user;
}

mctx make_root()
{
	mctx root;
	root["users"] = mctx::make_array();
	root["users"].push_back(make_user(0, "Ada", "admin", true));
	root["users"].push_back(make_user(1, "Lin", "user", true));
	root["users"].push_back(make_user(2, "Grace", "admin", false));
	root["users"][0]["code"] = "a&b\"c";
	root["users"][2]["score"] = std::int64_t{-7};
	root["quoted/key\""] = "escaped";
	return root;
}

void path_and_serialization_tests()
{
	auto root = make_root();
	const ContextPath empty;
	require(&empty[root] == &root, "empty path must select its input");
	require(empty.getAllMatching(root).size() == 1, "empty path must have one match");

	const auto name_path =
		ContextPath(ContextPath::root) / "users" / 1 / "name";
	require(name_path[root].as<std::string>() == "Lin", "key/index traversal failed");
	require(name_path.stringify() == "#/\"users\"/1/\"name\"", "canonical path changed");

	const auto reparsed = ContextPathSerializer::deserialize(name_path.stringify());
	require(reparsed[root].as<std::string>() == "Lin", "serialized path did not round-trip");
	const auto literal_path = "#/\"users\"/0/\"name\""_ctxpath;
	require(literal_path[root].as<std::string>() == "Ada", "path literal traversal failed");

	const auto escaped = ContextPath(ContextPath::root) / "quoted/key\"";
	const auto escaped_text = escaped.stringify();
	require(ContextPathSerializer::deserialize(escaped_text)[root].as<std::string>() == "escaped",
		"quoted key escaping did not round-trip");

	const auto relative = ContextPath("settings") / "enabled";
	relative[root] = true;
	require(root["settings"]["enabled"].get<bool>(), "mutable key traversal did not create objects");

	const auto normalized = ContextPath(ContextPath::root) / "users" / ContextPath::previous;
	require(normalized.stringify() == "#", "previous segment did not normalize composition");
	const auto root_barrier = ContextPath(ContextPath::root) / ContextPath::previous;
	require(root_barrier.stringify() == "#", "previous segment escaped above root");

	require_throws<std::invalid_argument>([]
	{
		static_cast<void>(ContextPathSerializer::deserialize("#/"));
	}, "trailing delimiter was accepted");
	require_throws<std::invalid_argument>([]
	{
		static_cast<void>(ContextPathSerializer::deserialize("#/-1"));
	}, "negative index was accepted");
	require_throws<std::invalid_argument>([]
	{
		static_cast<void>(ContextPathSerializer::deserialize("\"unterminated"));
	}, "unterminated string was accepted");
}

void wildcard_and_query_tests()
{
	auto root = make_root();
	const auto all_names = ContextPathSerializer::deserialize("#/\"users\"/*/\"name\"");
	const auto names = all_names.getAllMatching(root);
	require(names.size() == 3, "wildcard did not select every array element");
	require(names[0]->as<std::string>() == "Ada" && names[2]->as<std::string>() == "Grace",
		"wildcard result order changed");
	require(all_names.existsAny(root), "existsAny rejected wildcard matches");
	require(all_names.existsAll(root), "existsAll rejected a complete wildcard path");

	root["users"][1].erase("name");
	require(!all_names.existsAll(root), "existsAll ignored a missing wildcard descendant");

	auto bound = all_names;
	bound.setVariables(2);
	require(bound[root].as<std::string>() == "Grace", "bound wildcard index failed");
	require(bound.stringify() == all_names.stringify(), "variable binding changed path serialization");
	require_throws<std::out_of_range>([&]
	{
		bound.setVariables(0);
	}, "excess variable assignment was accepted");

	root["lookup"]["first"]["second"] = "value";
	auto two_variables = ContextPathSerializer::deserialize("#/\"lookup\"/*/*");
	two_variables.setVariables("first", "second");
	require(two_variables[root].as<std::string>() == "value",
		"sequential variable binding reused the first wildcard");

	const auto active_admin_names = ContextPathSerializer::deserialize(
		"#/\"users\"/*:{*/\"role\"=\"admin\"&*/\"active\"=true}/\"name\"");
	const auto admins = active_admin_names.getAllMatching(root);
	require(admins.size() == 1 && admins.front()->as<std::string>() == "Ada",
		"multiquery selector returned the wrong elements");

	const auto id_zero = ContextPathSerializer::deserialize(
		"#/\"users\"/*:{*/\"id\"=0}/\"role\"");
	require(id_zero[root].as<std::string>() == "admin", "integral query failed for zero");
	const auto negative_score = ContextPathSerializer::deserialize(
		"#/\"users\"/*:{*/\"score\"=-7}/\"id\"");
	require(negative_score[root].get<std::uint64_t>() == 2,
		"negative integral query failed");

	const auto escaped_query = ContextPath(ContextPath::root) / "users" /
		ContextPath::whereAll({ContextPath::queryEquals(
			ContextPath("code"), mctx(std::string("a&b\"c")))}) / "id";
	const auto escaped_query_round_trip =
		ContextPathSerializer::deserialize(escaped_query.stringify());
	require(escaped_query_round_trip[root].get<std::uint64_t>() == 0,
		"escaped query string did not round-trip");

	root["users"][0]["marker"];
	const auto has_marker = ContextPathSerializer::deserialize(
		"#/\"users\"/*:{*/\"marker\"=*}/\"id\"");
	require(has_marker.getAllMatching(root).size() == 1,
		"presence query rejected an existing null field");

	const auto predicate_path = ContextPath(ContextPath::root) / "users" /
		ContextPath::where([](const mctx& value)
		{
			return value.is_object() && value.contains("role") &&
				value.at("role").as<std::string>() == "user";
		}) / "id";
	require(predicate_path[root].get<std::uint64_t>() == 1,
		"programmatic predicate selected the wrong element");

	require_throws<std::invalid_argument>([]
	{
		static_cast<void>(ContextPathSerializer::deserialize("*:{}"));
	}, "empty query set was accepted");
}

void removal_tests()
{
	auto root = make_root();
	const auto all_names = ContextPathSerializer::deserialize("#/\"users\"/*/\"name\"");
	require(all_names.remove(root), "wildcard removal reported failure");
	for(const auto& user : root["users"])
		require(!user.contains("name"), "wildcard removal left a matched key behind");

	const auto first_user = ContextPath(ContextPath::root) / "users" / 0;
	require(first_user.remove(root), "array-index removal reported failure");
	require(root["users"].size() == 2 && root["users"][0]["id"].get<std::uint64_t>() == 1,
		"array-index removal erased the wrong element");

	const auto admins = ContextPathSerializer::deserialize(
		"#/\"users\"/*:{*/\"role\"=\"admin\"}");
	require(admins.remove(root), "query removal reported failure");
	require(root["users"].size() == 1 &&
		root["users"][0]["role"].as<std::string>() == "user",
		"query removal did not erase all selected elements");
}

void linked_context_tests()
{
	auto root = std::make_shared<mctx>(make_root());
	(*root)["selection"]["$ref"] = "#/\"users\"/*:{*/\"id\"=1}";
	(*root)["admins"]["$ref"] = "#/\"users\"/*:{*/\"role\"=\"admin\"}";
	(*root)["admins"]["$as_array"] = true;
	(*root)["direct"]["$ref"] = ContextPathSerializer::deserialize(
		"#/\"users\"/*:{*/\"id\"=0}");

	LinkedContextWrapper wrapper(root);
	require(wrapper["selection"]["name"].as<std::string>() == "Lin",
		"single linked-context navigation failed");
	require(wrapper["direct"]["name"].as<std::string>() == "Ada",
		"stored ContextPath reference navigation failed");
	const auto selection_iterator = wrapper.find("selection");
	require((*selection_iterator)["name"].as<std::string>() == "Lin",
		"linked iterator dereference did not resolve its reference");

	const auto rendered = wrapper.renderAsLinkedContext();
	require(rendered.at("selection").at("name").as<std::string>() == "Lin",
		"single reference did not render as its target");
	require(rendered.at("admins").is_array() && rendered.at("admins").size() == 2,
		"reference array did not render every target");

	mctx selection_diff;
	selection_diff["selection"]["name"]["+"] = "Linna";
	selection_diff["selection"]["name"]["-"] = "Lin";
	wrapper.applyRenderedContextDiff(selection_diff);
	require((*root)["users"][1]["name"].as<std::string>() == "Linna",
		"rendered diff was not forwarded through a reference");
	require((*root)["selection"].contains("$ref"), "diff forwarding replaced the reference node");

	mctx first_admin_diff;
	first_admin_diff["name"]["+"] = "Ada Lovelace";
	first_admin_diff["name"]["-"] = "Ada";
	mctx admin_array_diff = mctx::make_array();
	admin_array_diff.push_back(std::move(first_admin_diff));
	mctx root_array_diff;
	root_array_diff["admins"] = std::move(admin_array_diff);
	wrapper.applyRenderedContextDiff(root_array_diff);
	require((*root)["users"][0]["name"].as<std::string>() == "Ada Lovelace",
		"array diff was not forwarded to its linked target");

	auto clone = wrapper.deepClone();
	mctx first_user_clone_diff;
	first_user_clone_diff["name"]["+"] = "Clone only";
	first_user_clone_diff["name"]["-"] = "Ada Lovelace";
	mctx users_clone_diff = mctx::make_array();
	users_clone_diff.push_back(std::move(first_user_clone_diff));
	mctx clone_diff;
	clone_diff["users"] = std::move(users_clone_diff);
	clone.applyRenderedContextDiff(clone_diff);
	require(clone["users"][ContextPath(0)]["name"].as<std::string>() == "Clone only",
		"deep clone was not independently mutable");
	require((*root)["users"][0]["name"].as<std::string>() == "Ada Lovelace",
		"deep clone mutation leaked into the source");

	auto cyclic_root = std::make_shared<mctx>();
	(*cyclic_root)["loop"]["$ref"] = "#/\"loop\"";
	LinkedContextWrapper cyclic(cyclic_root);
	require_throws<std::runtime_error>([&]
	{
		static_cast<void>(cyclic.renderAsLinkedContext());
	}, "reference cycle was not detected");
}

} // namespace

int main()
{
	try
	{
		path_and_serialization_tests();
		wildcard_and_query_tests();
		removal_tests();
		linked_context_tests();
		std::cout << "context path tests passed\n";
		return 0;
	}
	catch(const std::exception& error)
	{
		std::cerr << "context path tests failed: " << error.what() << '\n';
		return 1;
	}
}
