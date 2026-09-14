#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <vector>

#include "buffered_block_list.h"

#ifdef _MSC_VER
// This test deliberately instantiates over-aligned storage. MSVC reports the
// required padding as C4324 even though preserving that alignment is the test.
#pragma warning(disable: 4324)
#endif

namespace
{

void require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

struct counted_value
{
	inline static int live = 0;
	int value;

	explicit counted_value(int value) : value(value) { ++live; }
	counted_value(const counted_value& other) : value(other.value) { ++live; }
	~counted_value() { --live; }
};

struct throwing_value
{
	inline static bool should_throw = false;
	int value;

	explicit throwing_value(int value) : value(value)
	{
		if (should_throw)
			throw std::runtime_error("requested construction failure");
	}
};

struct alignas(64) over_aligned_value
{
	int value;

	explicit over_aligned_value(int value) : value(value) {}
};

template<typename Range>
std::vector<int> values(const Range& range)
{
	std::vector<int> result;
	for (const auto& value : range)
		result.push_back(value);
	return result;
}

void test_end_pushes_and_iteration()
{
	using list_type = dixelu::buffered_block_list<int>;
	static_assert(list_type::block_capacity == 256);
	static_assert(std::bidirectional_iterator<list_type::iterator>);
	static_assert(std::bidirectional_iterator<list_type::const_iterator>);
	using eight_byte_list = dixelu::buffered_block_list<std::uint64_t>;
	static_assert(eight_byte_list::value_bytes_per_block == 2048);
	static_assert(eight_byte_list::offset_bytes_per_block == 512);
	static_assert(eight_byte_list::control_block_bytes < 3000,
		"separate offset arrays must avoid eight-byte value slot padding");

	list_type list;
	require(list.empty() && list.size() == 0, "a new block list must be empty");
	require(list.begin() == list.end(), "an empty block list must have an empty range");

	for (int value = 0; value < 600; ++value)
		list.push_back(value);
	require(list.size() == 600, "push_back must keep an exact constant-time size");
	require(list.block_count() == 3, "600 back pushes must occupy three 256-slot blocks");
	require(list.front() == 0 && list.back() == 599, "front and back must cross block boundaries");

	int expected = 0;
	for (int value : list)
		require(value == expected++, "forward iteration must preserve push_back order");

	expected = 599;
	for (auto current = list.rbegin(); current != list.rend(); ++current)
		require(*current == expected--, "reverse iteration must cross block boundaries");

	for (int value = 1; value <= 300; ++value)
		list.push_front(-value);
	require(list.size() == 900, "mixed end pushes must preserve size");
	require(list.block_count() == 5, "mixed end pushes must reuse free circular slots");
	require(list.front() == -300 && list.back() == 599, "mixed end pushes must preserve endpoints");

	for (int value = -300; value < 600; ++value)
	{
		require(list.front() == value, "pop_front must preserve order");
		list.pop_front();
	}
	require(list.empty() && list.block_count() == 0, "erasing every value must release every block");

	list_type circular;
	for (int value = 0; value < 128; ++value)
		circular.push_back(value);
	for (int value = 1; value <= 128; ++value)
		circular.push_front(-value);
	require(circular.size() == 256 && circular.block_count() == 1,
		"circular slot indices must let front and back growth fill one complete block");
	for (int value = -128; value < 128; ++value)
	{
		require(circular.front() == value, "circular offset traversal must preserve mixed order");
		circular.pop_front();
	}
}

void test_stability_and_constant_time_erase()
{
	dixelu::buffered_block_list<int> list;
	for (int value = 0; value < 700; ++value)
		list.push_back(value);

	auto stable = list.begin();
	std::advance(stable, 400);
	int* stable_address = std::addressof(*stable);

	auto erased = list.begin();
	std::advance(erased, 255);
	auto following = list.erase(erased);
	require(*following == 256, "erase must return the following element across an offset gap");
	require(list.size() == 699, "erase must update size");
	require(*stable == 400 && std::addressof(*stable) == stable_address,
		"unrelated iterators and references must remain stable");

	erased = list.begin();
	std::advance(erased, 255);
	following = list.erase(erased);
	require(*following == 257, "repeated erase must bridge a larger byte offset");

	list.pop_back();
	require(list.back() == 698 && list.size() == 697, "pop_back must erase the tail in O(1)");
}

void test_opportunistic_insertion()
{
	dixelu::buffered_block_list<int> list;
	list.push_back(1);
	list.push_back(2);
	list.push_back(3);

	auto position = list.begin();
	std::advance(position, 2);
	require(!list.try_insert(position, 9),
		"try_insert must refuse physically adjacent middle elements");

	position = list.begin();
	++position;
	list.erase(position);
	position = list.begin();
	++position;
	auto inserted = list.try_insert(position, 2);
	require(inserted && **inserted == 2, "try_insert must reuse an erased in-block gap");
	require(values(list) == std::vector<int>({1, 2, 3}),
		"gap insertion must restore logical order");

	auto at_front = list.try_insert(list.begin(), 0);
	auto at_back = list.try_insert(list.end(), 4);
	require(at_front && at_back && values(list) == std::vector<int>({0, 1, 2, 3, 4}),
		"try_insert at an endpoint must use mandatory end insertion");

	dixelu::buffered_block_list<int> cross_block;
	for (int value = 0; value <= 256; ++value)
		cross_block.push_back(value);
	auto second_block = cross_block.begin();
	std::advance(second_block, 256);
	auto old_tail = second_block;
	--old_tail;
	cross_block.erase(old_tail);
	second_block = cross_block.begin();
	std::advance(second_block, 255);
	inserted = cross_block.try_insert(second_block, 255);
	require(inserted && **inserted == 255,
		"try_insert must reuse end space beside a block boundary");
}

void test_lifetimes_move_only_values_and_exceptions()
{
	{
		dixelu::buffered_block_list<counted_value> list;
		for (int value = 0; value < 300; ++value)
			list.emplace_back(value);
		for (int value = 0; value < 100; ++value)
			list.pop_front();
		require(counted_value::live == 200, "erase must destroy exactly one stored value");
		list.clear();
		require(counted_value::live == 0, "clear must destroy every live value");
	}
	require(counted_value::live == 0, "destruction must not leak live values");

	dixelu::buffered_block_list<std::unique_ptr<int>> move_only;
	move_only.push_back(std::make_unique<int>(7));
	move_only.emplace_front(std::make_unique<int>(6));
	require(**move_only.begin() == 6 && **move_only.rbegin() == 7,
		"end insertion must support move-only values");

	dixelu::buffered_block_list<over_aligned_value> over_aligned;
	for (int value = 0; value < 300; ++value)
	{
		auto& inserted = over_aligned.emplace_back(value);
		require(
			reinterpret_cast<std::uintptr_t>(std::addressof(inserted)) % alignof(over_aligned_value) == 0,
			"pooled element storage must preserve over-alignment");
	}

	dixelu::buffered_block_list<throwing_value> throwing;
	for (int value = 0; value < 256; ++value)
		throwing.emplace_back(value);
	throwing_value::should_throw = true;
	try
	{
		throwing.emplace_back(256);
		require(false, "requested construction failure must propagate");
	}
	catch (const std::runtime_error&)
	{}
	throwing_value::should_throw = false;
	require(throwing.size() == 256 && throwing.block_count() == 1 &&
		throwing.front().value == 0 && throwing.back().value == 255,
		"failed new-block construction must leave the list unchanged");
	throwing.emplace_back(256);
	require(throwing.size() == 257 && throwing.block_count() == 2,
		"the pool must remain usable after failed new-block construction");
}

void test_deterministic_mixed_operations()
{
	dixelu::buffered_block_list<int> list;
	std::vector<int> reference;
	std::uint32_t state = 0xC001D00Du;
	int next_value = 0;
	auto random = [&] {
		state = state * 1664525u + 1013904223u;
		return state;
	};

	for (int operation = 0; operation < 12000; ++operation)
	{
		const std::uint32_t choice = random() % 5;
		if (reference.empty() || choice == 0)
		{
			list.push_back(next_value);
			reference.push_back(next_value++);
		}
		else if (choice == 1)
		{
			list.push_front(next_value);
			reference.insert(reference.begin(), next_value++);
		}
		else if (choice == 2)
		{
			const std::size_t index = random() % reference.size();
			auto position = list.begin();
			std::advance(position, static_cast<std::ptrdiff_t>(index));
			auto following = list.erase(position);
			reference.erase(reference.begin() + static_cast<std::ptrdiff_t>(index));
			if (index == reference.size())
				require(following == list.end(), "tail erase must return end");
			else
				require(*following == reference[index], "erase must return the model's next value");
		}
		else if (choice == 3)
		{
			const std::size_t index = random() % (reference.size() + 1);
			auto position = list.begin();
			std::advance(position, static_cast<std::ptrdiff_t>(index));
			const int value = next_value++;
			if (auto inserted = list.try_insert(position, value))
			{
				require(**inserted == value, "successful try_insert must return its new value");
				reference.insert(
					reference.begin() + static_cast<std::ptrdiff_t>(index),
					value);
			}
		}
		else if ((random() & 1u) == 0)
		{
			list.pop_front();
			reference.erase(reference.begin());
		}
		else
		{
			list.pop_back();
			reference.pop_back();
		}

		if (operation % 37 == 0)
		{
			require(list.size() == reference.size(), "mixed operations must keep exact size");
			require(values(list) == reference, "mixed operations must match the vector model");
			std::vector<int> reversed(list.rbegin(), list.rend());
			require(
				std::equal(reversed.begin(), reversed.end(), reference.rbegin(), reference.rend()),
				"mixed reverse iteration must match the vector model");
		}
	}

	require(values(list) == reference, "the final mixed-operation state must match the model");
}

} // namespace

int main()
{
	test_end_pushes_and_iteration();
	test_stability_and_constant_time_erase();
	test_opportunistic_insertion();
	test_lifetimes_move_only_values_and_exceptions();
	test_deterministic_mixed_operations();
}
