#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include "midi_overlap_filter.h"

namespace
{

struct span
{
	std::uint64_t start_tick;
	std::uint64_t end_tick;
	std::uint32_t track_n;
	std::uint32_t id;
};

void require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}

std::vector<std::uint32_t> ids(const std::vector<span>& spans)
{
	std::vector<std::uint32_t> result;
	for (const auto& value : spans)
		result.push_back(value.id);
	return result;
}

void test_exact_priority_rule()
{
	constexpr auto held = (std::numeric_limits<std::uint64_t>::max)();
	std::vector<span> input{
		// Equal tick arrives out of priority order. Lower is longer: draw both.
		{100, 140, 10, 2}, {100, 150, 5, 1},
		// Equal length: strictly higher priority removes the lower note.
		{200, 230, 10, 4}, {200, 230, 5, 3},
		// Higher priority is longer: remove the lower note.
		{300, 330, 10, 6}, {300, 320, 5, 5},
		// Higher priority is shorter: draw both.
		{400, 430, 10, 8}, {400, 450, 5, 7},
		// An unfinished higher note cannot prove that the lower is redundant.
		{500, held, 10, 10}, {500, 550, 5, 9},
		// An unfinished lower note cannot itself be removed yet.
		{600, 650, 10, 12}, {600, held, 5, 11},
		// Equal priority is not greater priority, so neither duplicate suppresses.
		{700, 730, 7, 13}, {700, 740, 7, 14},
		// A different source tick never participates, even with identical geometry.
		{701, 740, 15, 15},
	};
	std::vector<span> scratch;

	dixelu::remove_redundant_midi_overlaps(input, scratch);

	const std::vector<std::uint32_t> expected{
		1, 2, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	require(ids(input) == expected,
		"filter must implement the exact same-tick/same-key/greater-priority/length rule");
}

void test_higher_priority_is_track_then_channel()
{
	std::vector<span> input{
		{10, 20, (2u << 4) | 15u, 1},
		{10, 20, (3u << 4) | 0u, 2},
	};
	std::vector<span> scratch;
	dixelu::remove_redundant_midi_overlaps(input, scratch);
	require(input.size() == 1 && input.front().id == 2,
		"track * 16 + channel must define the complete priority ordering");
}

void test_randomized_against_direct_rule()
{
	constexpr auto held = (std::numeric_limits<std::uint64_t>::max)();
	std::uint32_t random_state = 0xC001D00Du;
	auto random = [&]()
	{
		random_state = random_state * 1664525u + 1013904223u;
		return random_state;
	};

	for (std::uint32_t round = 0; round < 200; ++round)
	{
		std::vector<span> input;
		for (std::uint64_t tick = 0; tick < 8; ++tick)
		{
			const auto group_size = 1u + random() % 64u;
			for (std::uint32_t index = 0; index < group_size; ++index)
			{
				const auto ending = (random() & 15u) == 0 ? held :
					tick * 100 + 1 + random() % 100u;
				input.push_back({tick, ending, random() % 96u,
					static_cast<std::uint32_t>(input.size())});
			}
		}

		std::vector<span> expected;
		std::size_t group_begin = 0;
		while (group_begin < input.size())
		{
			std::size_t group_end = group_begin + 1;
			while (group_end < input.size() &&
				input[group_end].start_tick == input[group_begin].start_tick)
				++group_end;

			std::vector<span> group(input.begin() + group_begin, input.begin() + group_end);
			std::stable_sort(group.begin(), group.end(),
				[](const span& left, const span& right)
				{
					return left.track_n < right.track_n;
				});
			for (const auto& candidate : group)
			{
				bool redundant = false;
				if (candidate.end_tick != held)
				{
					for (const auto& possible_cover : group)
					{
						if (possible_cover.track_n > candidate.track_n &&
							possible_cover.end_tick != held &&
							possible_cover.end_tick >= candidate.end_tick)
						{
							redundant = true;
							break;
						}
					}
				}
				if (!redundant)
					expected.push_back(candidate);
			}
			group_begin = group_end;
		}

		std::vector<span> actual = input;
		std::vector<span> scratch;
		dixelu::remove_redundant_midi_overlaps(actual, scratch);
		require(ids(actual) == ids(expected),
			"linear filter must match the direct strict rule for randomized groups");
	}
}

void test_linear_million_note_group()
{
	constexpr std::size_t note_count = 1'000'000;
	std::vector<span> input;
	input.reserve(note_count);
	for (std::size_t index = note_count; index-- > 0;)
		input.push_back({42, 84, static_cast<std::uint32_t>(index),
			static_cast<std::uint32_t>(index)});
	std::vector<span> scratch;

	dixelu::remove_redundant_midi_overlaps(input, scratch);

	require(input.size() == 1 && input.front().track_n == note_count - 1,
		"a million-note equal-tick group must collapse without quadratic scans");
}

} // namespace

int main()
{
	try
	{
		test_exact_priority_rule();
		test_higher_priority_is_track_then_channel();
		test_randomized_against_direct_rule();
		test_linear_million_note_group();
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
