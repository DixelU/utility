#pragma once

#include <atomic>
#include <cstddef>
#include <iterator>
#include <new>
#include <utility>

#if defined(_MSC_VER)
#define DIXELU_SPSC_FORCE_INLINE __forceinline
#define DIXELU_SPSC_NO_INLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define DIXELU_SPSC_FORCE_INLINE inline __attribute__((always_inline))
#define DIXELU_SPSC_NO_INLINE __attribute__((noinline))
#else
#define DIXELU_SPSC_FORCE_INLINE inline
#define DIXELU_SPSC_NO_INLINE
#endif

namespace dixelu
{

// An unbounded single-producer/single-consumer queue with stable element
// addresses. Producer-only operations: emplace(), push(), back().
// Consumer-only operations: front(), pop(), empty(), begin(), end(), clear().
// Iteration captures a read-only boundary and permits concurrent pushes, but
// pop() and clear() must not run until that traversal finishes. Destruction and
// clear() require both sides to be stopped.
template<
	typename T,
	std::size_t SlabSize = (1u << 15),
	std::size_t MaxRecycledSlabs = 6>
class buffered_queue_spsc
{
	static_assert(SlabSize > 0);

	struct slab
	{
		alignas(64) alignas(T) std::byte data[sizeof(T) * SlabSize];
		T* begin;
		std::atomic<T*> end;
		std::atomic<slab*> next_slab;

		slab() :
			begin(data_begin()),
			end(data_begin()),
			next_slab(nullptr)
		{}

		T* data_begin() noexcept
		{
			return reinterpret_cast<T*>(data);
		}

		T* capacity_end() noexcept
		{
			return data_begin() + SlabSize;
		}

		[[nodiscard]] bool empty_consumer() const noexcept
		{
			return begin == end.load(std::memory_order_acquire);
		}

		[[nodiscard]] DIXELU_SPSC_FORCE_INLINE bool full_producer() noexcept
		{
			return end.load(std::memory_order_relaxed) == capacity_end();
		}

		template<typename... Args>
		DIXELU_SPSC_FORCE_INLINE T& emplace_producer(Args&&... args)
		{
			T* current_end = end.load(std::memory_order_relaxed);
			::new (current_end) T(std::forward<Args>(args)...);
			end.store(current_end + 1, std::memory_order_release);
			return *current_end;
		}

		void pop_consumer() noexcept
		{
			begin->~T();
			++begin;
		}

		void reset_for_reuse() noexcept
		{
			begin = data_begin();
			end.store(data_begin(), std::memory_order_relaxed);
			next_slab.store(nullptr, std::memory_order_relaxed);
		}

		void clear_consumer() noexcept
		{
			while (!empty_consumer())
				pop_consumer();
		}
	};

	alignas(64) slab* tail_ = nullptr;
	std::size_t pushed_local_ = 0;

	// Published only on slab rollover, allowing O(1) snapshot capture without
	// making the producer's per-element tail access atomic.
	alignas(64) std::atomic<slab*> published_tail_{nullptr};
	// First slab publication keeps lazy allocation race-free: the producer
	// writes this once and the consumer adopts it into its private head_.
	alignas(64) std::atomic<slab*> published_head_{nullptr};

	alignas(64) slab* head_ = nullptr;
	std::size_t popped_local_ = 0;
	bool front_ready_ = false;

	alignas(64) std::atomic<slab*> recycle_head_{nullptr};
	alignas(64) std::atomic<std::size_t> recycle_count_{0};
	alignas(64) std::atomic<std::size_t> pushed_{0};
	alignas(64) std::atomic<std::size_t> popped_{0};

	slab* producer_get_slab()
	{
		slab* recycled = recycle_head_.load(std::memory_order_acquire);
		while (recycled)
		{
			slab* next = recycled->next_slab.load(std::memory_order_relaxed);
			if (recycle_head_.compare_exchange_weak(
				recycled,
				next,
				std::memory_order_acquire,
				std::memory_order_relaxed))
			{
				recycle_count_.fetch_sub(1, std::memory_order_relaxed);
				recycled->reset_for_reuse();
				return recycled;
			}
		}
		return new slab();
	}

	void consumer_recycle_slab(slab* reusable) noexcept
	{
		if constexpr (MaxRecycledSlabs == 0)
		{
			delete reusable;
			return;
		}

		if (recycle_count_.load(std::memory_order_relaxed) >= MaxRecycledSlabs)
		{
			delete reusable;
			return;
		}

		slab* old_head = recycle_head_.load(std::memory_order_relaxed);
		do
		{
			reusable->next_slab.store(old_head, std::memory_order_relaxed);
		} while (!recycle_head_.compare_exchange_weak(
			old_head,
			reusable,
			std::memory_order_release,
			std::memory_order_relaxed));
		recycle_count_.fetch_add(1, std::memory_order_relaxed);
	}

	DIXELU_SPSC_FORCE_INLINE void ensure_initialized_producer()
	{
		if (!tail_)
		{
			tail_ = producer_get_slab();
			published_tail_.store(tail_, std::memory_order_release);
			published_head_.store(tail_, std::memory_order_release);
		}
	}

	[[nodiscard]] DIXELU_SPSC_FORCE_INLINE slab* consumer_head() noexcept
	{
		slab* current = head_;
		if (!current)
		{
			current = published_head_.load(std::memory_order_acquire);
			head_ = current;
		}
		return current;
	}

	[[nodiscard]] DIXELU_SPSC_FORCE_INLINE bool prepare_consumer_front() noexcept
	{
		if (front_ready_)
			return true;

		slab* current = consumer_head();
		while (current)
		{
			if (!current->empty_consumer())
			{
				front_ready_ = true;
				return true;
			}

			slab* next = current->next_slab.load(std::memory_order_acquire);
			if (!next)
				return false;
			consumer_recycle_slab(current);
			current = next;
			head_ = current;
		}
		return false;
	}

	template<typename... Args>
	DIXELU_SPSC_NO_INLINE T& emplace_in_new_slab(Args&&... args)
	{
		slab* next = producer_get_slab();
		T* result;
		try
		{
			result = &next->emplace_producer(std::forward<Args>(args)...);
		}
		catch (...)
		{
			delete next;
			throw;
		}
		tail_->next_slab.store(next, std::memory_order_release);
		tail_ = next;
		published_tail_.store(next, std::memory_order_release);
		return *result;
	}

public:
	class consumer_iterator
	{
		slab* current_slab_ = nullptr;
		slab* stop_slab_ = nullptr;
		T* current_ = nullptr;
		T* slab_end_ = nullptr;
		T* stop_end_ = nullptr;

		void finish() noexcept
		{
			current_slab_ = nullptr;
			stop_slab_ = nullptr;
			current_ = nullptr;
			slab_end_ = nullptr;
			stop_end_ = nullptr;
		}

		void advance_to_readable_position() noexcept
		{
			while (current_slab_ && current_ == slab_end_)
			{
				if (current_slab_ == stop_slab_)
				{
					finish();
					return;
				}

				slab* next = current_slab_->next_slab.load(std::memory_order_acquire);
				if (!next)
				{
					finish();
					return;
				}

				current_slab_ = next;
				current_ = next->begin;
				slab_end_ = next == stop_slab_ ?
					stop_end_ : next->end.load(std::memory_order_acquire);
			}
		}

	public:
		using iterator_concept = std::forward_iterator_tag;
		using iterator_category = std::forward_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = const T*;
		using reference = const T&;

		consumer_iterator() noexcept = default;

		consumer_iterator(slab* first_slab, slab* stop_slab, T* stop_end) noexcept :
			current_slab_(first_slab),
			stop_slab_(stop_slab),
			current_(first_slab ? first_slab->begin : nullptr),
			slab_end_(first_slab == stop_slab ?
				stop_end : first_slab->end.load(std::memory_order_acquire)),
			stop_end_(stop_end)
		{
			advance_to_readable_position();
		}

		[[nodiscard]] reference operator*() const noexcept
		{
			return *current_;
		}

		[[nodiscard]] pointer operator->() const noexcept
		{
			return current_;
		}

		consumer_iterator& operator++() noexcept
		{
			++current_;
			advance_to_readable_position();
			return *this;
		}

		consumer_iterator operator++(int) noexcept
		{
			consumer_iterator previous = *this;
			++(*this);
			return previous;
		}

		[[nodiscard]] friend bool operator==(
			const consumer_iterator& iterator,
			std::default_sentinel_t) noexcept
		{
			return iterator.current_slab_ == nullptr;
		}

		[[nodiscard]] friend bool operator==(
			std::default_sentinel_t sentinel,
			const consumer_iterator& iterator) noexcept
		{
			return iterator == sentinel;
		}

		[[nodiscard]] friend bool operator==(
			const consumer_iterator& left,
			const consumer_iterator& right) noexcept
		{
			return left.current_slab_ == right.current_slab_ &&
				left.current_ == right.current_;
		}
	};

	buffered_queue_spsc() = default;

	~buffered_queue_spsc()
	{
		static_cast<void>(consumer_head());
		while (head_)
		{
			slab* next = head_->next_slab.load(std::memory_order_relaxed);
			head_->clear_consumer();
			delete head_;
			head_ = next;
		}

		slab* recycled = recycle_head_.load(std::memory_order_relaxed);
		while (recycled)
		{
			slab* next = recycled->next_slab.load(std::memory_order_relaxed);
			delete recycled;
			recycled = next;
		}
	}

	buffered_queue_spsc(const buffered_queue_spsc&) = delete;
	buffered_queue_spsc& operator=(const buffered_queue_spsc&) = delete;
	buffered_queue_spsc(buffered_queue_spsc&&) = delete;
	buffered_queue_spsc& operator=(buffered_queue_spsc&&) = delete;

	template<typename... Args>
	DIXELU_SPSC_FORCE_INLINE T& emplace(Args&&... args)
	{
		ensure_initialized_producer();

		T* result = tail_->full_producer() ?
			&emplace_in_new_slab(std::forward<Args>(args)...) :
			&tail_->emplace_producer(std::forward<Args>(args)...);
		pushed_.store(++pushed_local_, std::memory_order_relaxed);
		return *result;
	}

	DIXELU_SPSC_FORCE_INLINE void push(const T& value)
	{
		emplace(value);
	}

	DIXELU_SPSC_FORCE_INLINE void push(T&& value)
	{
		emplace(std::move(value));
	}

	[[nodiscard]] T& back() noexcept
	{
		return *(tail_->end.load(std::memory_order_relaxed) - 1);
	}

	[[nodiscard]] const T& back() const noexcept
	{
		return const_cast<buffered_queue_spsc*>(this)->back();
	}

	DIXELU_SPSC_FORCE_INLINE void pop() noexcept
	{
		if (!prepare_consumer_front())
			return;

		front_ready_ = false;
		head_->pop_consumer();
		popped_.store(++popped_local_, std::memory_order_relaxed);
		static_cast<void>(prepare_consumer_front());
	}

	[[nodiscard]] DIXELU_SPSC_FORCE_INLINE bool empty() const noexcept
	{
		return !const_cast<buffered_queue_spsc*>(this)->prepare_consumer_front();
	}

	[[nodiscard]] DIXELU_SPSC_FORCE_INLINE T& front() noexcept
	{
		static_cast<void>(prepare_consumer_front());
		return *head_->begin;
	}

	[[nodiscard]] const T& front() const noexcept
	{
		return const_cast<buffered_queue_spsc*>(this)->front();
	}

	void clear() noexcept
	{
		front_ready_ = false;
		static_cast<void>(consumer_head());
		while (head_)
		{
			head_->clear_consumer();
			slab* next = head_->next_slab.load(std::memory_order_relaxed);
			if (next)
			{
				consumer_recycle_slab(head_);
				head_ = next;
			}
			else
			{
				head_->reset_for_reuse();
				tail_ = head_;
				published_tail_.store(head_, std::memory_order_relaxed);
				published_head_.store(head_, std::memory_order_relaxed);
				break;
			}
		}

		pushed_local_ = 0;
		popped_local_ = 0;
		pushed_.store(0, std::memory_order_relaxed);
		popped_.store(0, std::memory_order_relaxed);
	}

	[[nodiscard]] std::size_t approximate_size() const noexcept
	{
		const std::size_t pushed = pushed_.load(std::memory_order_relaxed);
		const std::size_t popped = popped_.load(std::memory_order_relaxed);
		return pushed > popped ? pushed - popped : 0;
	}

	// Captures the currently published queue boundary. Elements pushed after
	// this call are deliberately deferred until the next traversal.
	[[nodiscard]] consumer_iterator begin() noexcept
	{
		if (!prepare_consumer_front())
			return {};

		slab* stop_slab = published_tail_.load(std::memory_order_acquire);
		T* stop_end = stop_slab->end.load(std::memory_order_acquire);
		return consumer_iterator(head_, stop_slab, stop_end);
	}

	[[nodiscard]] std::default_sentinel_t end() const noexcept
	{
		return {};
	}
};

} // namespace dixelu

#undef DIXELU_SPSC_FORCE_INLINE
#undef DIXELU_SPSC_NO_INLINE
