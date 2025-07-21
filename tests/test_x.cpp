#include "unique_soo_ptr.h"
#include "spoilable_future.h"

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cassert>
#include <functional>

// Custom data type for testing
class TestData
{
public:
	explicit TestData(int val = 0) : value(val) {}
	TestData(const TestData& other) : value(other.value) { ++copyCount; }
	TestData(TestData&& other) noexcept : value(other.value) { ++moveCount; other.value = 0; }
	~TestData() = default;

	TestData& operator=(const TestData& other)
	{
		if (this != &other)
		{
			value = other.value;
			++copyCount;
		}
		return *this;
	}

	TestData& operator=(TestData&& other) noexcept
	{
		if (this != &other)
		{
			value = other.value;
			other.value = 0;
			++moveCount;
		}
		return *this;
	}

	[[nodiscard]] int getValue() const { return value; }

	static void resetCounters() { copyCount = 0; moveCount = 0; }
	static int getCopyCount() { return copyCount; }
	static int getMoveCount() { return moveCount; }

private:
	int value;
	static inline int copyCount = 0;
	static inline int moveCount = 0;
};

// Helper function to test promise states
void test_assert(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "ASSERTION FAILED: " << message << std::endl;
		assert(condition);
	}
}

// Basic test for promise and future
void test_basic_future()
{
	std::cout << "\n=== Testing basic future/promise ===" << std::endl;

	dixelu::promise<int> p;
	dixelu::future<int> f = p.get_future();

	test_assert(f.valid(), "Future should be valid after get_future");

	std::thread producer([&p]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		p.set_value(42);
	});

	int result = f.get();
	test_assert(result == 42, "Expected value 42, got " + std::to_string(result));
	test_assert(f.get_state() == dixelu::future_status::moved_from,
			  "Future state should be moved_from after get() on non-shared future");

	producer.join();
	std::cout << "✓ Basic future/promise test passed" << std::endl;
}

// Test for shared futures
void test_shared_future()
{
	std::cout << "\n=== Testing shared future ===" << std::endl;

	dixelu::shared_promise<int> p;
	dixelu::shared_future<int> f1 = p.get_future();
	dixelu::shared_future<int> f2 = f1; // Copy construction

	test_assert(f1.valid(), "First shared future should be valid");
	test_assert(f2.valid(), "Second shared future should be valid");

	p.set_value(123);

	int result1 = f1.get();
	int result2 = f2.get();

	test_assert(result1 == 123, "First future should get value 123");
	test_assert(result2 == 123, "Second future should get value 123");
	test_assert(f1.get_state() == dixelu::future_status::ready,
			  "Shared future state should remain ready after get()");

	// Get value again from a shared future
	int result3 = f1.get();
	test_assert(result3 == 123, "Should be able to get value again from shared future");

	std::cout << "✓ Shared future test passed" << std::endl;
}

// Test for waitless futures
void test_waitless_future()
{
	std::cout << "\n=== Testing waitless future ===" << std::endl;

	dixelu::waitless_promise<int> p;
	dixelu::waitless_future<int> f = p.get_future();

	bool exception_thrown = false;
	try
	{
		f.get(); // Should throw since the future is not ready
	}
	catch (const std::runtime_error& e)
	{
		exception_thrown = true;
		std::cout << "Expected exception: " << e.what() << std::endl;
	}

	test_assert(exception_thrown, "Exception should be thrown for non-ready waitless future");

	p.set_value(99);

	int result = f.get();
	test_assert(result == 99, "Expected value 99, got " + std::to_string(result));

	std::cout << "✓ Waitless future test passed" << std::endl;
}

// Test for reusable futures
void test_reusable_future()
{
	std::cout << "\n=== Testing reusable future ===" << std::endl;

	dixelu::reusable_promise<int> p;
	dixelu::reusable_future<int> f = p.get_future();

	p.set_value(10);

	int result1 = f.get();
	test_assert(result1 == 10, "First value should be 10");

	// Set a new value
	p.set_value(20);

	int result2 = f.get();
	test_assert(result2 == 20, "Second value after reset should be 20");

	std::cout << "✓ Reusable future test passed" << std::endl;
}

// Test for move semantics and memory operations
void test_move_semantics()
{
	std::cout << "\n=== Testing move semantics ===" << std::endl;

	TestData::resetCounters();

	dixelu::promise<TestData> p;
	dixelu::future<TestData> f = p.get_future();

	p.set_value(TestData(42));

	TestData result = f.get();
	test_assert(result.getValue() == 42, "Value should be 42");
	test_assert(TestData::getMoveCount() >= 1, "Move constructor should be used");

	std::cout << "Move count: " << TestData::getMoveCount() << std::endl;
	std::cout << "Copy count: " << TestData::getCopyCount() << std::endl;
	std::cout << "✓ Move semantics test passed" << std::endl;
}

// Test for promise destruction and future spoiling
void test_promise_destruction()
{
	std::cout << "\n=== Testing promise destruction and future spoiling ===" << std::endl;

	dixelu::future<int> f;

	{
		dixelu::promise<int> p;
		f = p.get_future();
		// p goes out of scope and is destroyed here
	}

	// Wait for the future to be notified of promise destruction
	auto status = f.wait();
	test_assert(status == dixelu::future_status::spoiled, "Future should be spoiled when promise is destroyed");

	bool exception_thrown = false;
	try
	{
		f.get();
	}
	catch (const std::runtime_error& e)
	{
		exception_thrown = true;
		std::cout << "Expected exception: " << e.what() << std::endl;
	}

	test_assert(exception_thrown, "Exception should be thrown for spoiled future");

	std::cout << "✓ Promise destruction test passed" << std::endl;
}

// Test for waiting with timeout
void test_wait_for()
{
	std::cout << "\n=== Testing wait_for ===" << std::endl;

	dixelu::promise<int> p;
	dixelu::future<int> f = p.get_future();

	const auto start = std::chrono::steady_clock::now();
	auto status = f.wait_for(std::chrono::milliseconds(100));
	const auto end = std::chrono::steady_clock::now();

	const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	test_assert(duration >= 90, "wait_for should wait for the specified duration");
	test_assert(status == dixelu::future_status::yet_empty, "Status should be yet_empty after timeout");

	std::thread producer([&p]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		p.set_value(77);
	});

	status = f.wait_for(std::chrono::milliseconds(200));
	test_assert(status == dixelu::future_status::ready, "Status should be ready after value is set");

	const int result = f.get();
	test_assert(result == 77, "Value should be 77");

	producer.join();
	std::cout << "✓ wait_for test passed" << std::endl;
}

// Test for shared_waitless_reusable variant
void test_shared_waitless_reusable()
{
	std::cout << "\n=== Testing shared_waitless_reusable future ===" << std::endl;

	dixelu::shared_waitless_reusable_promise<std::string> p;
	dixelu::shared_waitless_reusable_future<std::string> f1 = p.get_future();

	// Initially not ready
	bool exception_thrown = false;
	try
	{
		f1.get();
	}
	catch (const std::runtime_error&)
	{
		exception_thrown = true;
	}

	test_assert(exception_thrown, "Exception should be thrown for non-ready waitless future");

	// Set value
	p.set_value("hello");

	// Create a copy
	dixelu::shared_waitless_reusable_future<std::string> f2 = f1;

	// Both should get the same value
	std::string result1 = f1.get();
	std::string result2 = f2.get();
	test_assert(result1 == "hello", "First future should get 'hello'");
	test_assert(result2 == "hello", "Second future should get 'hello'");

	// Reset and set a new value
	p.reset();

	exception_thrown = false;
	try
	{
		f1.get();
	}
	catch (const std::runtime_error&)
	{
		exception_thrown = true;
	}
	test_assert(exception_thrown, "Exception should be thrown after reset");

	p.set_value("world");
	result1 = f1.get();
	result2 = f2.get();
	test_assert(result1 == "world", "First future should get 'world' after reset");
	test_assert(result2 == "world", "Second future should get 'world' after reset");

	std::cout << "✓ Shared waitless reusable future test passed" << std::endl;
}

// Main test runner
int main()
{
	std::cout << "Starting spoilable_future tests..." << std::endl;

	try
	{
		test_basic_future();
		test_shared_future();
		test_waitless_future();
		test_reusable_future();
		test_move_semantics();
		test_promise_destruction();
		test_wait_for();
		test_shared_waitless_reusable();

		std::cout << "\nALL TESTS PASSED!" << std::endl;
	}
	catch (const std::exception& e)
	{
		std::cerr << "\nTest failed with exception: " << e.what() << std::endl;
		return 1;
	}

	return 0;
}