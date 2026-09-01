#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "midi_visual_note_store.h"

namespace
{

void require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

using store_type = dixelu::midi_visual_note_store<2, 1024>;

std::size_t visible_count(const store_type& store, std::size_t key)
{
	std::size_t result = 0;
	for (auto* note = store.first_visible(key); note; note = note->visible_next)
		++result;
	return result;
}

std::size_t drain_updates(store_type& store, std::int64_t cutoff)
{
	for (int attempt = 0; attempt < 32; ++attempt)
	{
		const auto visible = store.update(cutoff);
		if (store.get_statistics().waiting_for_renderer == 0)
			return visible;
	}
	throw std::runtime_error("renderer lifecycle backlog did not drain");
}

void test_held_note_does_not_block_completed_retirement()
{
	store_type store;
	auto* held = store.create(0, 0, 1);

	constexpr std::size_t completed_count = 250'000;
	for (std::size_t index = 0; index < completed_count; ++index)
	{
		auto* note = store.create(0, index + 1, 2);
		store.finish(note, index + 2);
	}
	store.flush();

	require(drain_updates(store, static_cast<std::int64_t>(completed_count + 3)) == 1,
		"an earlier held note must not pin later completed notes");
	store.reclaim();

	const auto after_completed = store.get_statistics();
	require(after_completed.live == 1 && after_completed.visible == 1,
		"completed notes must return to the pool while the held note remains live");
	require(visible_count(store, 0) == 1 && store.first_visible(0) == held,
		"the held note must remain drawable after completed notes retire");

	store.finish(held, completed_count + 4);
	store.flush();
	require(drain_updates(store, static_cast<std::int64_t>(completed_count + 5)) == 0,
		"the held note must retire after its own ending arrives");
	store.reclaim();
	require(store.get_statistics().live == 0,
		"every retired note must be reclaimed");
}

void test_non_nested_end_order_erases_in_constant_time()
{
	store_type store;
	auto* first = store.create(1, 0, 10, 123);
	auto* second = store.create(1, 1, 11);
	auto* third = store.create(1, 2, 12);

	store.finish(first, 10, 456);
	store.finish(third, 20);
	store.flush();
	require(first->start_tick == 123 &&
		first->end_tick.load(std::memory_order_relaxed) == 456,
		"exact source ticks must survive the producer-to-renderer handoff");
	store.update(15);
	store.reclaim();

	require(visible_count(store, 1) == 2,
		"retiring the first note must preserve unrelated visible notes");
	require(store.first_visible(1) == second && second->visible_next == third,
		"intrusive removal must preserve start-time drawing order");

	store.finish(second, 21);
	store.flush();
	store.update(22);
	store.reclaim();
	require(store.get_statistics().live == 0,
		"non-nested note endings must all retire without a scan or blocked front");
}

void test_reset_releases_all_lifetimes()
{
	store_type store;
	for (std::size_t index = 0; index < 10'000; ++index)
		static_cast<void>(store.create(index & 1, index, static_cast<std::uint32_t>(index)));
	store.flush();
	store.update(-1);
	require(store.get_statistics().live == 10'000,
		"test setup must publish every held note");

	store.reset();
	const auto reset = store.get_statistics();
	require(reset.live == 0 && reset.visible == 0 &&
		reset.waiting_for_renderer == 0 && reset.waiting_for_reclaim == 0,
		"reset must release notes and queued pointer state");
}

void test_renderer_work_is_bounded_per_update()
{
	store_type store;
	constexpr std::size_t note_count = 300'000;
	for (std::size_t index = 0; index < note_count; ++index)
		static_cast<void>(store.create(0, index, static_cast<std::uint32_t>(index)));
	store.flush();

	const auto first_visible = store.update(-1);
	require(first_visible < note_count,
		"one render update must not absorb an arbitrarily large parser backlog");
	require(store.get_statistics().waiting_for_renderer != 0,
		"bounded update must leave excess lifecycle work for a later frame");
	require(drain_updates(store, -1) == note_count,
		"subsequent frames must drain the complete lifecycle backlog");
}

void test_concurrent_immediate_completion_publication()
{
	store_type store;
	std::atomic_bool producer_done = false;
	constexpr std::size_t note_count = 100'000;

	std::thread producer([&]
	{
		for (std::size_t index = 0; index < note_count; ++index)
		{
			auto* note = store.create(0, index, static_cast<std::uint32_t>(index));
			store.finish(note, index);
		}
		store.flush();
		producer_done.store(true, std::memory_order_release);
	});

	while (!producer_done.load(std::memory_order_acquire))
	{
		store.update((std::numeric_limits<std::int64_t>::max)());
		std::this_thread::yield();
	}
	producer.join();

	// Drain lifecycle events that may have been published immediately after the
	// consumer swapped out the previous batch.
	for (int attempt = 0; attempt < 3; ++attempt)
		store.update((std::numeric_limits<std::int64_t>::max)());
	store.reclaim();

	const auto drained = store.get_statistics();
	if (drained.live != 0 || drained.visible != 0)
		std::cerr << "live=" << drained.live << " visible=" << drained.visible
			<< " waiting_renderer=" << drained.waiting_for_renderer
			<< " waiting_reclaim=" << drained.waiting_for_reclaim << '\n';
	require(drained.live == 0 && drained.visible == 0,
		"immediately completed concurrent notes must not be lost during handoff");
}

} // namespace

int main()
{
	try
	{
		test_held_note_does_not_block_completed_retirement();
		test_non_nested_end_order_erases_in_constant_time();
		test_reset_releases_all_lifetimes();
		test_renderer_work_is_bounded_per_update();
		test_concurrent_immediate_completion_publication();
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
