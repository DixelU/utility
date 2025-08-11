#define BOOST_TEST_MODULE mctx_test

#include <boost/test/included/unit_test.hpp>

#include <iostream>
#include <string>
#include <vector>

#include "mctx.h"

// Helper function to check if an exception is thrown
template<class T_Function>
bool check_exception(T_Function&& func)
{
	try
	{
		func();
		return false;
	}
	catch (const std::exception&)
	{
		return true;
	}
}


BOOST_AUTO_TEST_SUITE(mctx_suite)

BOOST_AUTO_TEST_CASE(scalar_types_test)
{
	// Test creation and retrieval of different scalar types
	mctx i = 42;
	BOOST_CHECK(i.is<int>());
	BOOST_CHECK_EQUAL(i.get<int>(), 42);

	mctx f = 3.14f;
	BOOST_CHECK(f.is<float>());
	BOOST_CHECK_CLOSE(f.get<float>(), 3.14f, 0.0001);

	mctx d = 2.71828;
	BOOST_CHECK(d.is<double>());
	BOOST_CHECK_CLOSE(d.get<double>(), 2.71828, 0.000001);

	mctx b = true;
	BOOST_CHECK(b.is<bool>());
	BOOST_CHECK_EQUAL(b.get<bool>(), true);

	mctx s = "hello";
	BOOST_CHECK(s.is<std::string>());
	BOOST_CHECK_EQUAL(s.get<std::string>(), "hello");

	mctx u = 10000000000ULL;
	BOOST_CHECK(u.is<uint64_t>());
	BOOST_CHECK_EQUAL(u.get<uint64_t>(), 10000000000ULL);

	// Test copy and move semantics
	mctx i2 = i;
	BOOST_CHECK(i2.is<int>());
	BOOST_CHECK_EQUAL(i2.get<int>(), 42);

	mctx s_move = std::move(s);
	BOOST_CHECK(s_move.is<std::string>());
	BOOST_CHECK_EQUAL(s_move.get<std::string>(), "hello");
	BOOST_CHECK(s.empty());
}

BOOST_AUTO_TEST_CASE(object_test)
{
	// Test object creation and key/value access
	mctx obj;
	BOOST_CHECK(obj.is_none());

	obj["name"] = "John Doe";
	obj["age"] = 30;
	obj["is_active"] = true;

	BOOST_CHECK(obj.is_object());
	BOOST_CHECK_EQUAL(obj.size(), 3);
	BOOST_CHECK_EQUAL(obj["name"].get<std::string>(), "John Doe");
	BOOST_CHECK_EQUAL(obj.at("age").get<int>(), 30);
	BOOST_CHECK_EQUAL(obj["is_active"].get<bool>(), true);

	// Test nested objects
	obj["address"]["city"] = "New York";
	BOOST_CHECK(obj["address"].is_object());
	BOOST_CHECK_EQUAL(obj["address"]["city"].get<std::string>(), "New York");

	// Test iterators
	int count = 0;
	for (const auto& item : obj)
	{
		// Just checking for iteration, not specific values
		count++;
	}
	BOOST_CHECK_EQUAL(count, 4); // name, age, is_active, address

	// Test find and erase
	auto it = obj.find("name");
	BOOST_CHECK(it != obj.end());
	BOOST_CHECK_EQUAL(it->get<std::string>(), "John Doe");

	obj.erase(it);
	BOOST_CHECK_EQUAL(obj.size(), 3);
	it = obj.find("name");
	BOOST_CHECK(it == obj.end());

	// Test bad access throws
	BOOST_CHECK(check_exception([&]() { obj.at("non_existent_key"); }));
}

BOOST_AUTO_TEST_CASE(array_test)
{
	// Test array creation and access
	mctx arr;
	BOOST_CHECK(arr.is_none());

	arr.push_back("one");
	arr.push_back(2);
	arr.push_back(3.0f);
	arr.push_back(true);

	BOOST_CHECK(arr.is_array());
	BOOST_CHECK_EQUAL(arr.size(), 4);
	BOOST_CHECK_EQUAL(arr[0].get<std::string>(), "one");
	BOOST_CHECK_EQUAL(arr.at(1).get<int>(), 2);

	// Test push_back
	mctx new_arr;
	new_arr.push_back("a");
	new_arr.push_back(10);
	BOOST_CHECK_EQUAL(new_arr.size(), 2);
	BOOST_CHECK_EQUAL(new_arr[0].get<std::string>(), "a");
	BOOST_CHECK_EQUAL(new_arr[1].get<int>(), 10);

	// Test nested arrays
	arr[4][0] = "nested";
	BOOST_CHECK(arr[4].is_array());
	BOOST_CHECK_EQUAL(arr[4][0].get<std::string>(), "nested");

	// Test iterators
	int count = 0;
	for (const auto& item : arr)
		count++;

	BOOST_CHECK_EQUAL(count, 5); // one, 2, 3.0, true, nested array

	// Test erase
	auto it = arr.begin();
	++it; // Point to the second element ("2")
	arr.erase(it);
	BOOST_CHECK_EQUAL(arr.size(), 4);
	BOOST_CHECK_EQUAL(arr[1].get<float>(), 3.0f); // The element at index 2 moved to index 1

	// Test bad access throws
	BOOST_CHECK(check_exception([&]() { arr.at(10); }));
}

BOOST_AUTO_TEST_CASE(comparison_test)
{
	mctx a = 10;
	mctx b = 10;
	mctx c = 20;

	BOOST_CHECK(a.as<int>() == b.get<int>());
	BOOST_CHECK(a.as<int>() != c.get<int>());
	BOOST_CHECK(b.as<int>() != c.get<int>());

	mctx s1 = "test";
	mctx s2 = "test";
	mctx s3 = "other";

	BOOST_CHECK(s1.as<std::string>() == s2.as<std::string>());
	BOOST_CHECK(s1.as<std::string>() != s3.as<std::string>());
}

BOOST_AUTO_TEST_CASE(custom_head_test)
{
	// Define a simple custom struct to test custom_head functionality
	struct MyStruct
	{
		std::string name;
		int id;
		bool operator==(const MyStruct& other) const { return name == other.name && id == other.id; }
	};

	mctx custom_val = MyStruct{"test", 123};
	BOOST_CHECK(custom_val.is<MyStruct>());

	MyStruct retrieved_val = custom_val.get<MyStruct>();
	BOOST_CHECK_EQUAL(retrieved_val.name, "test");
	BOOST_CHECK_EQUAL(retrieved_val.id, 123);

	// Test comparison
	mctx custom_val2 = MyStruct{"test", 123};
	mctx custom_val3 = MyStruct{"different", 456};

	BOOST_CHECK(custom_val.as<MyStruct>() == custom_val2.as<MyStruct>());
	BOOST_CHECK(custom_val.as<MyStruct>() != custom_val3.as<MyStruct>());
}

BOOST_AUTO_TEST_SUITE_END()
