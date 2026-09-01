#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace dixelu
{

// Removes only MIDI notes made completely redundant by a strictly higher
// track/channel priority at the same key and start tick. The caller supplies
// one key at a time in non-decreasing start-tick order. Equal-tick entries may
// arrive in any order; a stable, contiguous radix pass puts them into priority
// order without pointer-heavy trees or per-note allocations.
//
// Span must expose:
//   uint64-like start_tick, end_tick (~uint64_t{0} means still held)
//   uint32-like track_n ((track_index << 4) | channel)
template<typename Span>
void remove_redundant_midi_overlaps(
	std::vector<Span>& spans,
	std::vector<Span>& reorder_scratch)
{
	if (spans.size() < 2)
		return;

	constexpr std::uint64_t held = (std::numeric_limits<std::uint64_t>::max)();
	constexpr std::size_t radix_bits = 11;
	constexpr std::size_t radix_size = std::size_t{1} << radix_bits;
	constexpr std::uint32_t radix_mask = static_cast<std::uint32_t>(radix_size - 1);
	constexpr std::size_t insertion_sort_limit = 32;

	std::array<std::size_t, radix_size> counts{};
	reorder_scratch.resize(spans.size());

	auto sort_group = [&](std::size_t first, std::size_t last)
	{
		const std::size_t count = last - first;
		if (count < 2)
			return;

		if (count <= insertion_sort_limit)
		{
			for (std::size_t index = first + 1; index < last; ++index)
			{
				Span value = std::move(spans[index]);
				std::size_t position = index;
				while (position > first &&
					spans[position - 1].track_n > value.track_n)
				{
					spans[position] = std::move(spans[position - 1]);
					--position;
				}
				spans[position] = std::move(value);
			}
			return;
		}

		std::uint32_t maximum_track = 0;
		for (std::size_t index = first; index < last; ++index)
			maximum_track = (std::max)(maximum_track,
				static_cast<std::uint32_t>(spans[index].track_n));

		bool source_is_spans = true;
		for (std::size_t shift = 0;
			shift < std::numeric_limits<std::uint32_t>::digits &&
			(maximum_track >> shift) != 0;
			shift += radix_bits)
		{
			counts.fill(0);
			if (source_is_spans)
			{
				for (std::size_t index = first; index < last; ++index)
					++counts[(static_cast<std::uint32_t>(spans[index].track_n) >> shift) & radix_mask];
			}
			else
			{
				for (std::size_t index = first; index < last; ++index)
					++counts[(static_cast<std::uint32_t>(reorder_scratch[index].track_n) >> shift) & radix_mask];
			}

			std::size_t offset = first;
			for (auto& bucket_count : counts)
			{
				const auto next = offset + bucket_count;
				bucket_count = offset;
				offset = next;
			}

			if (source_is_spans)
			{
				for (std::size_t index = first; index < last; ++index)
				{
					const auto bucket =
						(static_cast<std::uint32_t>(spans[index].track_n) >> shift) & radix_mask;
					reorder_scratch[counts[bucket]++] = std::move(spans[index]);
				}
			}
			else
			{
				for (std::size_t index = first; index < last; ++index)
				{
					const auto bucket =
						(static_cast<std::uint32_t>(reorder_scratch[index].track_n) >> shift) & radix_mask;
					spans[counts[bucket]++] = std::move(reorder_scratch[index]);
				}
			}
			source_is_spans = !source_is_spans;
		}

		if (!source_is_spans)
			std::move(reorder_scratch.begin() + first,
				reorder_scratch.begin() + last, spans.begin() + first);
	};

	std::size_t output = 0;
	std::size_t group_begin = 0;
	while (group_begin < spans.size())
	{
		std::size_t group_end = group_begin + 1;
		while (group_end < spans.size() &&
			spans[group_end].start_tick == spans[group_begin].start_tick)
			++group_end;

#ifndef NDEBUG
		if (group_begin != 0)
			assert(spans[group_begin - 1].start_tick <= spans[group_begin].start_tick);
#endif

		sort_group(group_begin, group_end);

		// Work by equal-priority blocks. Notes in the same track/channel do not
		// suppress one another: the maximum is updated only after the whole block
		// has been decided against strictly higher priorities.
		std::size_t kept_begin = group_end;
		std::size_t block_end = group_end;
		std::uint64_t maximum_higher_end = 0;
		bool has_completed_higher = false;
		while (block_end > group_begin)
		{
			std::size_t block_begin = block_end - 1;
			const auto track = spans[block_begin].track_n;
			while (block_begin > group_begin &&
				spans[block_begin - 1].track_n == track)
				--block_begin;

			std::uint64_t maximum_block_end = 0;
			bool has_completed_in_block = false;
			for (std::size_t index = block_end; index-- > block_begin;)
			{
				const auto ending = static_cast<std::uint64_t>(spans[index].end_tick);
				const bool unfinished = ending == held;
				if (unfinished || !has_completed_higher || ending > maximum_higher_end)
					spans[--kept_begin] = std::move(spans[index]);

				if (!unfinished)
				{
					maximum_block_end = (std::max)(maximum_block_end, ending);
					has_completed_in_block = true;
				}
			}

			if (has_completed_in_block)
			{
				maximum_higher_end = has_completed_higher ?
					(std::max)(maximum_higher_end, maximum_block_end) : maximum_block_end;
				has_completed_higher = true;
			}
			block_end = block_begin;
		}

		for (std::size_t index = kept_begin; index < group_end; ++index)
			spans[output++] = std::move(spans[index]);
		group_begin = group_end;
	}

	spans.resize(output);
}

} // namespace dixelu
