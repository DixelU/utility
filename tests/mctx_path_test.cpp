#include "linked_mctx_wrapper.h"
#include "mctx_path_serializer.h"

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
using dixelu::mctx_path;
using dixelu::mctx_path_serializer;
using dixelu::linked_mctx_wrapper;
using dixelu::literals::operator""_mctx_path;

void require(bool condition, std::string_view message)
{
	if(!condition)
		throw std::runtime_error(std::string(message));
}

template<typename exception_type = std::exception, typename function_type>
void require_throws(function_type&& function, std::string_view message)
{
	try
	{
		std::invoke(std::forward<function_type>(function));
	}
	catch(const exception_type&)
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
	const mctx_path empty;
	require(&empty[root] == &root, "empty path must select its input");
	require(empty.get_all_matching(root).size() == 1, "empty path must have one match");

	const auto name_path =
		mctx_path(mctx_path::root) / "users" / 1 / "name";
	require(name_path[root].as<std::string>() == "Lin", "key/index traversal failed");
	require(name_path.stringify() == "#/\"users\"/1/\"name\"", "canonical path changed");

	const auto reparsed = mctx_path_serializer::deserialize(name_path.stringify());
	require(reparsed[root].as<std::string>() == "Lin", "serialized path did not round-trip");
	const auto literal_path = "#/\"users\"/0/\"name\""_mctx_path;
	require(literal_path[root].as<std::string>() == "Ada", "path literal traversal failed");

	const auto escaped = mctx_path(mctx_path::root) / "quoted/key\"";
	const auto escaped_text = escaped.stringify();
	require(mctx_path_serializer::deserialize(escaped_text)[root].as<std::string>() == "escaped",
		"quoted key escaping did not round-trip");

	const auto relative = mctx_path("settings") / "enabled";
	relative[root] = true;
	require(root["settings"]["enabled"].get<bool>(), "mutable key traversal did not create objects");

	const auto normalized = mctx_path(mctx_path::root) / "users" / mctx_path::previous;
	require(normalized.stringify() == "#", "previous segment did not normalize composition");
	const auto root_barrier = mctx_path(mctx_path::root) / mctx_path::previous;
	require(root_barrier.stringify() == "#", "previous segment escaped above root");

	require_throws<std::invalid_argument>([]
	{
		static_cast<void>(mctx_path_serializer::deserialize("#/"));
	}, "trailing delimiter was accepted");
	require_throws<std::invalid_argument>([]
	{
		static_cast<void>(mctx_path_serializer::deserialize("#/-1"));
	}, "negative index was accepted");
	require_throws<std::invalid_argument>([]
	{
		static_cast<void>(mctx_path_serializer::deserialize("\"unterminated"));
	}, "unterminated string was accepted");
}

void query_serialization_round_trip_tests()
{
	const std::string_view source =
		R"(#/data/*:{*/aasd/id=24&*/bxcv/qwer="ajsdhksjd654-asdflhfkjsdfh\""&*/"as"=null}/x29/asdjfkj)";
	const std::string_view expected =
		R"mctx(#/"data"/*:{*/"aasd"/"id"=24&*/"bxcv"/"qwer"="ajsdhksjd654-asdflhfkjsdfh\""&*/"as"=null}/"x29"/"asdjfkj")mctx";

	const auto parsed = mctx_path_serializer::deserialize(source);
	const auto canonical = mctx_path_serializer::serialize(parsed);
	require(canonical == expected, "query serialization did not canonicalize every segment");

	const auto reparsed = mctx_path_serializer::deserialize(canonical);
	require(mctx_path_serializer::serialize(reparsed) == canonical,
		"query serialization was not stable after a second parse");
}

void wildcard_and_query_tests()
{
	auto root = make_root();
	const auto all_names = mctx_path_serializer::deserialize("#/\"users\"/*/\"name\"");
	const auto names = all_names.get_all_matching(root);
	require(names.size() == 3, "wildcard did not select every array element");
	require(names[0]->as<std::string>() == "Ada" && names[2]->as<std::string>() == "Grace",
		"wildcard result order changed");
	require(all_names.exists_any(root), "exists_any rejected wildcard matches");
	require(all_names.exists_all(root), "exists_all rejected a complete wildcard path");

	root["users"][1].erase("name");
	require(!all_names.exists_all(root), "exists_all ignored a missing wildcard descendant");

	auto bound = all_names;
	bound.set_variables(2);
	require(bound[root].as<std::string>() == "Grace", "bound wildcard index failed");
	require(bound.stringify() == all_names.stringify(), "variable binding changed path serialization");
	require_throws<std::out_of_range>([&]
	{
		bound.set_variables(0);
	}, "excess variable assignment was accepted");

	root["lookup"]["first"]["second"] = "value";
	auto two_variables = mctx_path_serializer::deserialize("#/\"lookup\"/*/*");
	two_variables.set_variables("first", "second");
	require(two_variables[root].as<std::string>() == "value",
		"sequential variable binding reused the first wildcard");

	const auto active_admin_names = mctx_path_serializer::deserialize(
		"#/\"users\"/*:{*/\"role\"=\"admin\"&*/\"active\"=true}/\"name\"");
	const auto admins = active_admin_names.get_all_matching(root);
	require(admins.size() == 1 && admins.front()->as<std::string>() == "Ada",
		"multiquery selector returned the wrong elements");

	const auto id_zero = mctx_path_serializer::deserialize(
		"#/\"users\"/*:{*/\"id\"=0}/\"role\"");
	require(id_zero[root].as<std::string>() == "admin", "integral query failed for zero");
	const auto negative_score = mctx_path_serializer::deserialize(
		"#/\"users\"/*:{*/\"score\"=-7}/\"id\"");
	require(negative_score[root].get<std::uint64_t>() == 2,
		"negative integral query failed");

	const auto escaped_query = mctx_path(mctx_path::root) / "users" /
		mctx_path::where_all({mctx_path::query_equals(
			mctx_path("code"), mctx(std::string("a&b\"c")))}) / "id";
	const auto escaped_query_round_trip =
		mctx_path_serializer::deserialize(escaped_query.stringify());
	require(escaped_query_round_trip[root].get<std::uint64_t>() == 0,
		"escaped query string did not round-trip");

	root["users"][0]["marker"];
	const auto has_marker = mctx_path_serializer::deserialize(
		"#/\"users\"/*:{*/\"marker\"=*}/\"id\"");
	require(has_marker.get_all_matching(root).size() == 1,
		"presence query rejected an existing null field");

	const auto predicate_path = mctx_path(mctx_path::root) / "users" /
		mctx_path::where([](const mctx& value)
		{
			return value.is_object() && value.contains("role") &&
				value.at("role").as<std::string>() == "user";
		}) / "id";
	require(predicate_path[root].get<std::uint64_t>() == 1,
		"programmatic predicate selected the wrong element");

	require_throws<std::invalid_argument>([]
	{
		static_cast<void>(mctx_path_serializer::deserialize("*:{}"));
	}, "empty query set was accepted");
}

void query_matching_tests()
{
	auto root = make_root();
	auto add_permission = [](mctx& user, std::string scope, bool granted)
	{
		if(!user.contains("permissions"))
			user["permissions"] = mctx::make_array();
		mctx permission;
		permission["scope"] = std::move(scope);
		permission["granted"] = granted;
		user["permissions"].push_back(std::move(permission));
	};

	// Ada has the two required values, but in separate permissions. A nested
	// query must require both values from the same permission object.
	add_permission(root["users"][0], "billing", false);
	add_permission(root["users"][0], "profile", true);
	add_permission(root["users"][1], "billing", true);
	add_permission(root["users"][2], "billing", false);
	add_permission(root["users"][2], "billing", true);
	root["users"][0]["note"];
	root["users"][1]["note"] = "reviewed";

	const auto granted_billing_permission = mctx_path::where_all({
		mctx_path::query_equals(mctx_path("scope"), mctx(std::string("billing"))),
		mctx_path::query_equals(mctx_path("granted"), mctx(true))});
	const auto has_granted_billing = mctx_path(mctx_path::root) / "users" /
		mctx_path::where_all({mctx_path::query_exists(
			mctx_path("permissions") / granted_billing_permission)});
	const auto granted_billing_ids = has_granted_billing / "id";
	const auto billing_ids = granted_billing_ids.get_all_matching(root);
	require(billing_ids.size() == 2 && billing_ids[0]->get<std::uint64_t>() == 1 &&
		billing_ids[1]->get<std::uint64_t>() == 2,
		"nested query mixed values from different children");

	const auto round_trip = mctx_path_serializer::deserialize(granted_billing_ids.stringify());
	const auto round_trip_ids = round_trip.get_all_matching(root);
	require(round_trip_ids.size() == 2 && round_trip_ids[0]->get<std::uint64_t>() == 1 &&
		round_trip_ids[1]->get<std::uint64_t>() == 2,
		"nested query serialization changed its matches");

	const auto active_granted_billing = mctx_path(mctx_path::root) / "users" /
		mctx_path::where_all({
			mctx_path::query_exists(mctx_path("permissions") / granted_billing_permission),
			mctx_path::query_equals(mctx_path("active"), mctx(true))});
	const auto active_ids = (active_granted_billing / "id").get_all_matching(root);
	require(active_ids.size() == 1 && active_ids.front()->get<std::uint64_t>() == 1,
		"query clauses were not combined for the same candidate");

	const auto has_disabled_permission = mctx_path(mctx_path::root) / "users" /
		mctx_path::where_all({mctx_path::query_equals(
			mctx_path("permissions") / mctx_path::variable / "granted", mctx(false))}) / "id";
	const auto disabled_ids = has_disabled_permission.get_all_matching(root);
	require(disabled_ids.size() == 2 && disabled_ids[0]->get<std::uint64_t>() == 0 &&
		disabled_ids[1]->get<std::uint64_t>() == 2,
		"query equality did not accept a matching descendant among nonmatches");

	const auto null_note = mctx_path(mctx_path::root) / "users" /
		mctx_path::where_all({mctx_path::query_equals(mctx_path("note"), mctx{})}) / "id";
	const auto null_note_ids = null_note.get_all_matching(root);
	require(null_note_ids.size() == 1 && null_note_ids.front()->get<std::uint64_t>() == 0,
		"null equality query matched an absent or non-null value");

	const auto missing_scope = mctx_path(mctx_path::root) / "users" /
		mctx_path::where_all({mctx_path::query_equals(
			mctx_path("permissions") / mctx_path::variable / "scope", mctx(std::string("security")))});
	require(!missing_scope.exists_any(root), "non-matching query reported a result");

	auto removal_root = root;
	require(active_granted_billing.remove(removal_root), "nested query removal reported failure");
	require(removal_root["users"].size() == 2 &&
		removal_root["users"][0]["id"].get<std::uint64_t>() == 0 &&
		removal_root["users"][1]["id"].get<std::uint64_t>() == 2,
		"nested query removal selected the wrong users");
}

void removal_tests()
{
	auto root = make_root();
	const auto all_names = mctx_path_serializer::deserialize("#/\"users\"/*/\"name\"");
	require(all_names.remove(root), "wildcard removal reported failure");
	for(const auto& user : root["users"])
		require(!user.contains("name"), "wildcard removal left a matched key behind");

	const auto first_user = mctx_path(mctx_path::root) / "users" / 0;
	require(first_user.remove(root), "array-index removal reported failure");
	require(root["users"].size() == 2 && root["users"][0]["id"].get<std::uint64_t>() == 1,
		"array-index removal erased the wrong element");

	const auto admins = mctx_path_serializer::deserialize(
		"#/\"users\"/*:{*/\"role\"=\"admin\"}");
	require(admins.remove(root), "query removal reported failure");
	require(root["users"].size() == 1 &&
		root["users"][0]["role"].as<std::string>() == "user",
		"query removal did not erase all selected elements");
}

void linked_mctx_wrapper_tests()
{
	auto root = std::make_shared<mctx>(make_root());
	(*root)["selection"]["$ref"] = "#/\"users\"/*:{*/\"id\"=1}";
	(*root)["admins"]["$ref"] = "#/\"users\"/*:{*/\"role\"=\"admin\"}";
	(*root)["admins"]["$as_array"] = true;
	(*root)["direct"]["$ref"] = mctx_path_serializer::deserialize(
		"#/\"users\"/*:{*/\"id\"=0}");

	linked_mctx_wrapper wrapper(root);
	require(wrapper["selection"]["name"].as<std::string>() == "Lin",
		"single linked-context navigation failed");
	require(wrapper["direct"]["name"].as<std::string>() == "Ada",
		"stored mctx_path reference navigation failed");
	const auto selection_iterator = wrapper.find("selection");
	require((*selection_iterator)["name"].as<std::string>() == "Lin",
		"linked iterator dereference did not resolve its reference");

	const auto rendered = wrapper.render_as_linked_mctx();
	require(rendered.at("selection").at("name").as<std::string>() == "Lin",
		"single reference did not render as its target");
	require(rendered.at("admins").is_array() && rendered.at("admins").size() == 2,
		"reference array did not render every target");

	mctx selection_diff;
	selection_diff["selection"]["name"]["+"] = "Linna";
	selection_diff["selection"]["name"]["-"] = "Lin";
	wrapper.apply_rendered_mctx_diff(selection_diff);
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
	wrapper.apply_rendered_mctx_diff(root_array_diff);
	require((*root)["users"][0]["name"].as<std::string>() == "Ada Lovelace",
		"array diff was not forwarded to its linked target");

	auto clone = wrapper.deep_clone();
	mctx first_user_clone_diff;
	first_user_clone_diff["name"]["+"] = "Clone only";
	first_user_clone_diff["name"]["-"] = "Ada Lovelace";
	mctx users_clone_diff = mctx::make_array();
	users_clone_diff.push_back(std::move(first_user_clone_diff));
	mctx clone_diff;
	clone_diff["users"] = std::move(users_clone_diff);
	clone.apply_rendered_mctx_diff(clone_diff);
	require(clone["users"][mctx_path(0)]["name"].as<std::string>() == "Clone only",
		"deep clone was not independently mutable");
	require((*root)["users"][0]["name"].as<std::string>() == "Ada Lovelace",
		"deep clone mutation leaked into the source");

	auto cyclic_root = std::make_shared<mctx>();
	(*cyclic_root)["loop"]["$ref"] = "#/\"loop\"";
	linked_mctx_wrapper cyclic(cyclic_root);
	require_throws<std::runtime_error>([&]
	{
		static_cast<void>(cyclic.render_as_linked_mctx());
	}, "reference cycle was not detected");
}

} // namespace

int main()
{
	try
	{
		path_and_serialization_tests();
		query_serialization_round_trip_tests();
		wildcard_and_query_tests();
		query_matching_tests();
		removal_tests();
		linked_mctx_wrapper_tests();
		std::cout << "mctx_path tests passed\n";
		return 0;
	}
	catch(const std::exception& error)
	{
		std::cerr << "mctx_path tests failed: " << error.what() << '\n';
		return 1;
	}
}
