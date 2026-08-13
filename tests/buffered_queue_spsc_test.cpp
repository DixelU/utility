#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <vector>

#include "buffered_queue_spsc.h"

namespace
{

void require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void test_fifo()
{
	constexpr std::size_t count = 20'000;
	dixelu::buffered_queue_spsc<std::size_t, 31, 3> queue;
	std::atomic_bool producer_done = false;

	std::thread producer([&] {
		for (std::size_t index = 0; index < count; ++index)
			queue.emplace(index);
		producer_done.store(true, std::memory_order_release);
	});

	std::size_t expected = 0;
	while (!producer_done.load(std::memory_order_acquire) || !queue.empty())
	{
		if (queue.empty())
		{
			std::this_thread::yield();
			continue;
		}
		require(queue.front() == expected, "SPSC queue must preserve FIFO order");
		queue.pop();
		++expected;
	}

	producer.join();
	require(expected == count, "SPSC queue must deliver every produced value");
	require(queue.approximate_size() == 0, "drained SPSC queue must report zero size");
}

void test_snapshot_iteration()
{
	using queue_type = dixelu::buffered_queue_spsc<std::size_t, 3, 1>;
	queue_type queue;
	queue.emplace(0);
	queue.emplace(1);
	auto same_slab_snapshot = queue.begin();
	queue.emplace(2);

	std::vector<std::size_t> values;
	for (auto current = same_slab_snapshot; current != queue.end(); ++current)
		values.push_back(*current);
	require(
		values == std::vector<std::size_t>{0, 1},
		"snapshot iteration must defer later same-slab pushes");

	queue.emplace(3);
	queue.emplace(4);
	queue.emplace(5);
	auto slab_rollover_snapshot = queue.begin();
	queue.emplace(6);

	values.clear();
	for (auto current = slab_rollover_snapshot; current != queue.end(); ++current)
		values.push_back(*current);
	require(
		values == std::vector<std::size_t>{0, 1, 2, 3, 4, 5},
		"snapshot iteration must defer pushes into later slabs");

	values.clear();
	for (const auto value : queue)
		values.push_back(value);
	require(
		values == std::vector<std::size_t>{0, 1, 2, 3, 4, 5, 6},
		"a later snapshot must include newly published elements");

	while (!queue.empty())
		queue.pop();
	require(queue.begin() == queue.end(), "an empty snapshot must have no elements");
}

void test_snapshot_with_concurrent_producer()
{
	constexpr std::size_t initial_count = 8;
	constexpr std::size_t final_count = 10'000;
	dixelu::buffered_queue_spsc<std::size_t, initial_count, 2> queue;
	for (std::size_t index = 0; index < initial_count; ++index)
		queue.emplace(index);

	std::atomic_bool start_producer = false;
	std::thread producer([&] {
		while (!start_producer.load(std::memory_order_acquire))
			std::this_thread::yield();
		for (std::size_t index = initial_count; index < final_count; ++index)
			queue.emplace(index);
	});

	auto snapshot = queue.begin();
	start_producer.store(true, std::memory_order_release);

	std::size_t expected = 0;
	for (auto current = snapshot; current != queue.end(); ++current)
	{
		require(*current == expected, "concurrent snapshot must preserve its captured values");
		++expected;
		std::this_thread::yield();
	}
	producer.join();

	require(
		expected == initial_count,
		"concurrent producer pushes must be deferred past the captured boundary");

	expected = 0;
	for (const auto value : queue)
	{
		require(value == expected, "later snapshot must preserve FIFO order");
		++expected;
	}
	require(expected == final_count, "later snapshot must include every concurrent push");
}

} // namespace

int main()
{
	test_fifo();
	test_snapshot_iteration();
	test_snapshot_with_concurrent_producer();
}
