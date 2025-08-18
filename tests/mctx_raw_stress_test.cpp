#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <vector>

// Include the provided mctx.h and mctx_json.h content here
// For brevity, assuming they are included as headers.
// In a real setup, paste the content into respective header files.

#include "mctx.h"
#include "mctx_json.h"

using namespace dixelu;
using namespace mctx_json;

// Function to generate a random string
std::string random_string(size_t length)
{
	static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	std::string tmp_s;
	tmp_s.reserve(length);

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(0, sizeof(alphanum) - 2);

	for (size_t i = 0; i < length; ++i)
		tmp_s += alphanum[dis(gen)];

	return tmp_s;
}

// Stress test parameters

constexpr size_t NUM_ITERATIONS = 1000;		// Number of main loop iterations
constexpr size_t ARRAY_SIZE = 100;		// Size of arrays/objects
constexpr size_t NESTING_DEPTH = 4;		// Depth of nested structures
constexpr size_t STRING_LENGTH = 50;		// Average string length

// Function to build a deeply nested mctx structure
mctx build_nested_structure(size_t depth)
{
	if (depth == 0)
		return mctx(random_string(STRING_LENGTH));

	mctx obj = mctx::make_object();

	// Smaller size for deeper nesting to avoid explosion
	for (size_t i = 0; i < ARRAY_SIZE / 10; ++i)
	{
		std::string key = "key_" + std::to_string(i);

		if (i % 2 == 0)
			obj[key] = build_nested_structure(depth - 1);
		else
			obj[key] = static_cast<double>(rand());
	}

	return obj;
}

// Main stress test function
void stress_test()
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<> dis(0.0, 1.0);
	std::uniform_int_distribution<> int_dis(0, 100);

	auto start_time = std::chrono::high_resolution_clock::now();

	for (size_t iter = 0; iter < NUM_ITERATIONS; ++iter)
	{
		// Operation 1: Create and populate a large array
		mctx arr = mctx::make_array();

		for (size_t i = 0; i < ARRAY_SIZE; ++i)
		{
			if (i % 3 == 0)
				arr.push_back(static_cast<uint64_t>(int_dis(gen)));
			else if (i % 3 == 1)
				arr.push_back(dis(gen));
			else
				arr.push_back(random_string(STRING_LENGTH));
		}

		// Operation 2: Create and populate a large object
		mctx obj = mctx::make_object();
		for (size_t i = 0; i < ARRAY_SIZE; ++i)
		{
			std::string key = "key_" + std::to_string(i);

			if (i % 3 == 0)
				obj[key] = true;
			else if (i % 3 == 1)
				obj[key] = static_cast<float>(dis(gen));
			else
				obj[key] = random_string(STRING_LENGTH);
		}

		// Operation 3: Nested structures
		mctx nested = build_nested_structure(NESTING_DEPTH);

		// Operation 4: Access and modify elements
		for (size_t i = 0; i < ARRAY_SIZE; ++i)
		{
			if (arr.is_array() && i < arr.size())
			{
				auto& elem = arr[i];
				if (elem.is<uint64_t>())
					elem = elem.get<uint64_t>() + 1;

			}

			std::string key = "key_" + std::to_string(i);

			if (obj.is_object() && obj.find(key) != obj.end())
			{
				auto& val = obj[key];
				if (val.is<std::string>())
					val = val.get<std::string>() + "_modified";
			}
		}

		// Operation 5: Serialization and deserialization
		std::string serialized = serialize_pretty(obj);
		mctx deserialized = deserialize(serialized);

		// Operation 6: Comparison
		volatile bool equal = (obj == deserialized);

		// Operation 7: Erase elements (for array and object)
		if (arr.is_array())
		{
			auto it = arr.begin();

			while (it != arr.end())
			{
				if (!arr.empty() && rand() % 2 == 0)
					it = arr.erase(it);
				else
					++it;
			}
		}

		if (obj.is_object())
		{
			auto kv_it = obj.kvbegin();

			while (kv_it != obj.kvend())
			{
				if (kv_it->first.find("key_0") != std::string::npos)
					kv_it = obj.erase(kv_it);
				else
					++kv_it;
			}
		}

		// Operation 8: Clear structures
		arr.clear();
		obj.clear();
		nested.clear();

		// Prevent compiler optimizations from removing the work
		std::cout << iter << std::endl;  // Minimal output to show progress
	}

	auto end_time = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

	std::cout << "\n\tStress test completed in " << duration.count() << " mksec." << std::endl;
}

int main()
{
	stress_test();
	return 0;
}