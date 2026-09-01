#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include "buffered_object_pool.h"

namespace dixelu
{

// Cross-thread note lifetime store for a streaming MIDI visualiser.
//
// Producer (parser thread): create(), finish(), reclaim().
// Consumer (render thread): update(), first_visible().
// reset() requires both sides to be stopped. Notes keep stable addresses for
// the parser's per-track/key pending stacks, while the renderer owns an
// intrusive start-ordered list that supports O(1) removal once completed notes
// expire.
//
// Lifecycle notifications are exchanged in one ordered batch so an immediate
// note-off can never overtake its note-on. finish() calls are naturally in
// non-decreasing time order, making the renderer's completed list an expiry
// FIFO rather than a heap. Held notes never enter it and cannot block it.
template<
	std::size_t KeyCount = 128,
	std::size_t PoolSlabBytes = (1u << 20)>
class midi_visual_note_store
{
public:
	static constexpr std::size_t key_count = KeyCount;

	struct note
	{
		std::uint64_t start_time_us;
		std::atomic<std::uint64_t> end_time_us;
		std::uint64_t start_tick;
		std::atomic<std::uint64_t> end_tick;
		std::uint32_t track_id;
		note* live_previous = nullptr;           // producer-owned
		note* live_next = nullptr;               // producer-owned
		note* visible_previous = nullptr;        // consumer-owned
		note* visible_next = nullptr;            // consumer-owned
		note* completion_next = nullptr;         // consumer-owned
		std::uint8_t key;
		bool is_visible = false;                 // consumer-owned
		bool is_discarded = false;               // consumer-owned

		note(std::uint8_t new_key, std::uint64_t start, std::uint32_t track,
			std::uint64_t tick)
			: start_time_us(start), end_time_us(~std::uint64_t{0}),
			start_tick(tick), end_tick(~std::uint64_t{0}),
			track_id(track), key(new_key) {}

		note(const note&) = delete;
		note& operator=(const note&) = delete;
	};

	struct statistics
	{
		std::size_t live;
		std::size_t visible;
		std::size_t waiting_for_renderer;
		std::size_t waiting_for_reclaim;
	};

	midi_visual_note_store()
		: pool_(std::make_unique<pool_type>()) {}

	~midi_visual_note_store()
	{
		release_all();
	}

	midi_visual_note_store(const midi_visual_note_store&) = delete;
	midi_visual_note_store& operator=(const midi_visual_note_store&) = delete;
	midi_visual_note_store(midi_visual_note_store&&) = delete;
	midi_visual_note_store& operator=(midi_visual_note_store&&) = delete;

	// Allocate a stable note and publish it to the renderer.
	note* create(std::uint8_t key, std::uint64_t start_time_us,
		std::uint32_t track_id, std::uint64_t start_tick = 0)
	{
		if (key >= key_count)
			return nullptr;

		note* created = pool_->create(key, start_time_us, track_id, start_tick);
		link_live(created);
		try
		{
			publish({lifecycle_kind::addition, created});
		}
		catch (...)
		{
			unlink_live(created);
			pool_->destroy(created);
			--live_count_;
			throw;
		}
		maybe_maintain();
		return created;
	}

	// Publish a known ending. MIDI events reach this method chronologically, so
	// the renderer's completion FIFO is already ordered by end_time_us.
	void finish(note* completed, std::uint64_t end_time_us,
		std::uint64_t ending_tick = 0)
	{
		if (!completed)
			return;

		// Publish the exact ending tick before the release-store of end_time_us.
		// A renderer that observes the completed time can then safely consume the
		// tick with a relaxed load for strict overlap comparison.
		completed->end_tick.store(ending_tick, std::memory_order_relaxed);
		completed->end_time_us.store(end_time_us, std::memory_order_release);
		publish({lifecycle_kind::completion, completed});
		maybe_maintain();
	}

	// Publish the producer-private lifecycle batch. The parser calls this before
	// waiting and at end-of-stream; dense streams also flush automatically every
	// lifecycle_batch_size events. No mutex is taken for individual notes.
	void flush()
	{
		if (producer_lifecycle_.empty())
			return;

		{
			std::lock_guard lock(exchange_mutex_);
			if (pending_lifecycle_.empty())
				pending_lifecycle_.swap(producer_lifecycle_);
			else
			{
				pending_lifecycle_.insert(
					pending_lifecycle_.end(),
					producer_lifecycle_.begin(), producer_lifecycle_.end());
				producer_lifecycle_.clear();
			}
		}
		lifecycle_available_.store(true, std::memory_order_release);
	}

	// Return renderer-retired records to the producer-owned object pool.
	void reclaim()
	{
		if (!retired_available_.exchange(false, std::memory_order_acquire))
			return;

		producer_reclaims_.clear();
		{
			std::lock_guard lock(exchange_mutex_);
			producer_reclaims_.swap(pending_retired_);
		}

		for (note* released : producer_reclaims_)
		{
			unlink_live(released);
			pool_->destroy(released);
			--live_count_;
		}
		producer_reclaims_.clear();
	}

	// Incorporate new notes and retire every completed note older than cutoff.
	// Returns the number of records that remain drawable.
	std::size_t update(std::int64_t cutoff_time_us)
	{
		if (consumer_lifecycle_position_ == consumer_lifecycle_.size())
		{
			consumer_lifecycle_.clear();
			consumer_lifecycle_position_ = 0;
			if (lifecycle_available_.load(std::memory_order_acquire))
			{
				// Never stall the UI behind a producer publication. Missing this
				// particular swap only defers the batch until the next frame.
				std::unique_lock lock(exchange_mutex_, std::try_to_lock);
				if (lock.owns_lock())
				{
					consumer_lifecycle_.swap(pending_lifecycle_);
					lifecycle_available_.store(false, std::memory_order_relaxed);
				}
			}
		}
		drain_lifecycle(cutoff_time_us);

		std::size_t retired_this_update = 0;
		while (completion_head_ && retired_this_update < max_events_per_update)
		{
			note* expired = completion_head_;
			const auto ending = expired->end_time_us.load(std::memory_order_acquire);
			if (ending == ~std::uint64_t{0} ||
				static_cast<std::int64_t>(ending) >= cutoff_time_us)
				break;

			// Record the retirement before unlinking it. If vector growth fails,
			// the note remains visible and can be retried on the next update.
			consumer_retired_.push_back(expired);
			completion_head_ = expired->completion_next;
			if (!completion_head_)
				completion_tail_ = nullptr;
			expired->completion_next = nullptr;
			erase_visible(expired);
			++retired_this_update;
		}

		publish_retired();

		return visible_count_;
	}

	[[nodiscard]] const note* first_visible(std::size_t key) const noexcept
	{
		assert(key < key_count);
		return visible_head_[key];
	}

	[[nodiscard]] const note* last_visible(std::size_t key) const noexcept
	{
		assert(key < key_count);
		return visible_tail_[key];
	}

	// Diagnostic snapshot. Call only while producer and consumer are quiescent.
	[[nodiscard]] statistics get_statistics() const
	{
		std::lock_guard lock(exchange_mutex_);
		return {
			live_count_,
			visible_count_,
			producer_lifecycle_.size() + pending_lifecycle_.size() +
				(consumer_lifecycle_.size() - consumer_lifecycle_position_),
			pending_retired_.size()};
	}

	// Exclusive reset after parser and renderer activity has stopped. Replacing
	// the pool releases a previous polyphony spike instead
	// of retaining their high-watermark allocations for the process lifetime.
	void reset()
	{
		auto replacement = std::make_unique<pool_type>();
		release_all();
		pool_ = std::move(replacement);
	}

private:
	enum class lifecycle_kind : std::uint8_t { addition, completion };

	struct lifecycle_event
	{
		lifecycle_kind kind;
		note* value;
	};

	using pool_type = buffered_object_pool<note, PoolSlabBytes>;
	static constexpr std::size_t lifecycle_batch_size = 1024;
	static constexpr std::size_t max_events_per_update = 1u << 18;

	void maybe_maintain()
	{
		if ((++producer_operations_ & (lifecycle_batch_size - 1)) == 0)
		{
			flush();
			reclaim();
		}
	}

	void publish(lifecycle_event event)
	{
		producer_lifecycle_.push_back(event);
	}

	void publish_retired()
	{
		if (consumer_retired_.empty())
			return;

		std::lock_guard lock(exchange_mutex_);
		if (pending_retired_.empty())
			pending_retired_.swap(consumer_retired_);
		else
		{
			pending_retired_.insert(
				pending_retired_.end(), consumer_retired_.begin(), consumer_retired_.end());
			consumer_retired_.clear();
		}
		retired_available_.store(true, std::memory_order_release);
	}

	void link_live(note* added) noexcept
	{
		added->live_next = live_head_;
		if (live_head_)
			live_head_->live_previous = added;
		live_head_ = added;
		++live_count_;
	}

	void unlink_live(note* removed) noexcept
	{
		if (removed->live_previous)
			removed->live_previous->live_next = removed->live_next;
		else
			live_head_ = removed->live_next;
		if (removed->live_next)
			removed->live_next->live_previous = removed->live_previous;
		removed->live_previous = nullptr;
		removed->live_next = nullptr;
	}

	void drain_lifecycle(std::int64_t cutoff_time_us)
	{
		std::size_t processed = 0;
		while (consumer_lifecycle_position_ < consumer_lifecycle_.size() &&
			processed < max_events_per_update)
		{
			const auto event = consumer_lifecycle_[consumer_lifecycle_position_];
			note* changed = event.value;

			if (event.kind == lifecycle_kind::addition)
			{
				const auto ending = changed->end_time_us.load(std::memory_order_acquire);
				if (ending != ~std::uint64_t{0} &&
					static_cast<std::int64_t>(ending) < cutoff_time_us)
				{
					// The renderer fell behind both edges of this note. Keep the
					// record alive until its completion event is consumed, but never
					// add already-expired geometry to the visible lists.
					changed->is_discarded = true;
					++consumer_lifecycle_position_;
					++processed;
					continue;
				}

				changed->visible_previous = visible_tail_[changed->key];
				if (visible_tail_[changed->key])
					visible_tail_[changed->key]->visible_next = changed;
				else
					visible_head_[changed->key] = changed;
				visible_tail_[changed->key] = changed;
				changed->is_visible = true;
				++visible_count_;
				++consumer_lifecycle_position_;
				++processed;
				continue;
			}

			if (changed->is_discarded)
			{
				consumer_retired_.push_back(changed);
				changed->is_discarded = false;
				++consumer_lifecycle_position_;
				++processed;
				continue;
			}

			if (!changed->is_visible)
				throw std::logic_error("MIDI visual completion preceded its addition");
			if (completion_tail_)
				completion_tail_->completion_next = changed;
			else
				completion_head_ = changed;
			completion_tail_ = changed;
			++consumer_lifecycle_position_;
			++processed;
		}
	}

	void erase_visible(note* removed)
	{
		if (!removed->is_visible)
			throw std::logic_error("completed MIDI visual note is not in the visible list");
		if ((removed->visible_previous && removed->visible_previous->visible_next != removed) ||
			(!removed->visible_previous && visible_head_[removed->key] != removed) ||
			(removed->visible_next && removed->visible_next->visible_previous != removed) ||
			(!removed->visible_next && visible_tail_[removed->key] != removed))
			throw std::logic_error("MIDI visual note list links are inconsistent");
		if (removed->visible_previous)
			removed->visible_previous->visible_next = removed->visible_next;
		else
			visible_head_[removed->key] = removed->visible_next;
		if (removed->visible_next)
			removed->visible_next->visible_previous = removed->visible_previous;
		else
			visible_tail_[removed->key] = removed->visible_previous;
		removed->visible_previous = nullptr;
		removed->visible_next = nullptr;
		removed->is_visible = false;
		--visible_count_;
	}

	void release_all()
	{
		{
			std::lock_guard lock(exchange_mutex_);
			std::vector<lifecycle_event>().swap(producer_lifecycle_);
			std::vector<lifecycle_event>().swap(pending_lifecycle_);
			std::vector<note*>().swap(pending_retired_);
			lifecycle_available_.store(false, std::memory_order_relaxed);
			retired_available_.store(false, std::memory_order_relaxed);
		}
		std::vector<lifecycle_event>().swap(consumer_lifecycle_);
		consumer_lifecycle_position_ = 0;
		std::vector<note*>().swap(consumer_retired_);
		std::vector<note*>().swap(producer_reclaims_);

		visible_head_.fill(nullptr);
		visible_tail_.fill(nullptr);
		completion_head_ = nullptr;
		completion_tail_ = nullptr;
		visible_count_ = 0;

		while (live_head_)
		{
			note* current = live_head_;
			live_head_ = current->live_next;
			pool_->destroy(current);
		}
		live_count_ = 0;
		producer_operations_ = 0;
		pool_.reset();
	}

	std::unique_ptr<pool_type> pool_;
	mutable std::mutex exchange_mutex_;
	std::vector<lifecycle_event> producer_lifecycle_;
	std::vector<lifecycle_event> pending_lifecycle_;
	std::vector<note*> pending_retired_;
	std::vector<lifecycle_event> consumer_lifecycle_;
	std::size_t consumer_lifecycle_position_ = 0;
	std::vector<note*> consumer_retired_;
	std::vector<note*> producer_reclaims_;
	std::atomic<bool> lifecycle_available_{false};
	std::atomic<bool> retired_available_{false};
	std::array<note*, key_count> visible_head_{};
	std::array<note*, key_count> visible_tail_{};
	note* completion_head_ = nullptr;
	note* completion_tail_ = nullptr;
	note* live_head_ = nullptr;
	std::size_t live_count_ = 0;
	std::size_t visible_count_ = 0;
	std::size_t producer_operations_ = 0;
};

} // namespace dixelu
