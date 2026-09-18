#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "buffered_object_pool.h"

namespace dixelu
{

// A stable-address, single-threaded bidirectional list whose links are grouped
// into 256-element blocks. Value storage and the two offset tables are separate
// arrays, avoiding per-value link and alignment padding. Links inside a block
// are one-byte circular offsets; a zero offset crosses to the neighbouring
// block. Erase and size are O(1).
//
// Iterators and references remain valid across insertion. Erase invalidates
// only iterators and references to the erased element. The container itself is
// deliberately non-movable because its end iterator refers to its root link.
template<typename T, std::size_t TargetSlabBytes = (1u << 20)>
class buffered_block_list
{
	static_assert(std::is_object_v<T> && !std::is_array_v<T>);

	static constexpr std::size_t block_capacity_ = 256;

	struct block_link
	{
		block_link* next;
		block_link* previous;
		std::uint16_t live;
		std::uint8_t first;
		std::uint8_t last;
	};

	struct value_slot
	{
		alignas(T) std::byte storage[sizeof(T)];

		[[nodiscard]] T* value() noexcept
		{
			return std::launder(reinterpret_cast<T*>(storage));
		}

		[[nodiscard]] const T* value() const noexcept
		{
			return std::launder(reinterpret_cast<const T*>(storage));
		}
	};

	struct control_block : block_link
	{
		value_slot values[block_capacity_]{};
		/*[[indeterminate]]*/ std::uint8_t previous_offsets[block_capacity_]{};
		/*[[indeterminate]]*/ std::uint8_t next_offsets[block_capacity_]{};

		control_block() noexcept : block_link{nullptr, nullptr, 0, 0, 0} {}
	};

	using pool_type = buffered_object_pool<control_block, TargetSlabBytes>;

	block_link root_{nullptr, nullptr, 0, 0, 0};
	pool_type blocks_;
	std::size_t size_ = 0;
	std::size_t block_count_ = 0;

	[[nodiscard]] static control_block* as_block(block_link* link) noexcept
	{
		return static_cast<control_block*>(link);
	}

	[[nodiscard]] static const control_block* as_block(const block_link* link) noexcept
	{
		return static_cast<const control_block*>(link);
	}

	[[nodiscard]] static std::uint8_t advance_index(
		std::uint8_t index,
		std::uint8_t offset) noexcept
	{
		return static_cast<std::uint8_t>(
			static_cast<unsigned int>(index) + static_cast<unsigned int>(offset));
	}

	[[nodiscard]] static std::uint8_t retreat_index(
		std::uint8_t index,
		std::uint8_t offset) noexcept
	{
		return static_cast<std::uint8_t>(
			static_cast<unsigned int>(index) - static_cast<unsigned int>(offset));
	}

	[[nodiscard]] static bool can_append(const control_block& block) noexcept
	{
		return block.live < block_capacity_ &&
			advance_index(block.last, 1) != block.first;
	}

	[[nodiscard]] static bool can_prepend(const control_block& block) noexcept
	{
		return block.live < block_capacity_ &&
			retreat_index(block.first, 1) != block.last;
	}

	static void link_before(block_link* position, control_block* block) noexcept
	{
		block->next = position;
		block->previous = position->previous;
		position->previous->next = block;
		position->previous = block;
	}

	static void unlink(control_block* block) noexcept
	{
		block->previous->next = block->next;
		block->next->previous = block->previous;
	}

	template<typename... Args>
	T* construct_element(control_block& block, std::uint8_t index, Args&&... args)
	{
		return std::construct_at(
			reinterpret_cast<T*>(block.values[index].storage),
			std::forward<Args>(args)...);
	}

	template<typename... Args>
	std::pair<control_block*, std::uint8_t> create_block_before(
		block_link* position,
		std::uint8_t index,
		Args&&... args)
	{
		control_block* fresh = blocks_.create();
		try
		{
			construct_element(*fresh, index, std::forward<Args>(args)...);
		}
		catch (...)
		{
			blocks_.destroy(fresh);
			throw;
		}

		fresh->live = 1;
		fresh->first = index;
		fresh->last = index;
		fresh->previous_offsets[index] = 0;
		fresh->next_offsets[index] = 0;
		link_before(position, fresh);
		++size_;
		++block_count_;
		return {fresh, index};
	}

	template<typename... Args>
	std::uint8_t append_to(control_block& block, Args&&... args)
	{
		assert(can_append(block));
		const std::uint8_t index = advance_index(block.last, 1);
		construct_element(block, index, std::forward<Args>(args)...);

		block.previous_offsets[index] = 1;
		block.next_offsets[index] = 0;
		block.next_offsets[block.last] = 1;
		block.last = index;
		++block.live;
		++size_;
		return index;
	}

	template<typename... Args>
	std::uint8_t prepend_to(control_block& block, Args&&... args)
	{
		assert(can_prepend(block));
		const std::uint8_t index = retreat_index(block.first, 1);
		construct_element(block, index, std::forward<Args>(args)...);

		block.previous_offsets[index] = 0;
		block.next_offsets[index] = 1;
		block.previous_offsets[block.first] = 1;
		block.first = index;
		++block.live;
		++size_;
		return index;
	}

	template<bool IsConst>
	class basic_iterator
	{
		friend class buffered_block_list;
		template<bool>
		friend class basic_iterator;

		block_link* block_ = nullptr;
		std::uint8_t index_ = 0;

		basic_iterator(block_link* block, std::uint8_t index) noexcept :
			block_(block),
			index_(index)
		{}

	public:
		using iterator_category = std::bidirectional_iterator_tag;
		using iterator_concept = std::bidirectional_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using reference = std::conditional_t<IsConst, const T&, T&>;
		using pointer = std::conditional_t<IsConst, const T*, T*>;

		basic_iterator() noexcept = default;

		template<bool OtherConst>
			requires (IsConst && !OtherConst)
		basic_iterator(const basic_iterator<OtherConst>& other) noexcept :
			block_(other.block_),
			index_(other.index_)
		{}

		[[nodiscard]] reference operator*() const noexcept
		{
			if constexpr (IsConst)
				return *as_block(static_cast<const block_link*>(block_))->values[index_].value();
			else
				return *as_block(block_)->values[index_].value();
		}

		[[nodiscard]] pointer operator->() const noexcept
		{
			return std::addressof(operator*());
		}

		basic_iterator& operator++() noexcept
		{
			const std::uint8_t offset = as_block(block_)->next_offsets[index_];
			if (offset != 0)
			{
				index_ = advance_index(index_, offset);
			}
			else
			{
				block_ = block_->next;
				index_ = block_->live == 0 ? 0 : block_->first;
			}
			return *this;
		}

		basic_iterator operator++(int) noexcept
		{
			basic_iterator previous = *this;
			++*this;
			return previous;
		}

		basic_iterator& operator--() noexcept
		{
			if (block_->live == 0)
			{
				block_ = block_->previous;
				index_ = block_->last;
				return *this;
			}

			const std::uint8_t offset = as_block(block_)->previous_offsets[index_];
			if (offset != 0)
			{
				index_ = retreat_index(index_, offset);
			}
			else
			{
				block_ = block_->previous;
				index_ = block_->live == 0 ? 0 : block_->last;
			}
			return *this;
		}

		basic_iterator operator--(int) noexcept
		{
			basic_iterator previous = *this;
			--*this;
			return previous;
		}

		friend bool operator==(const basic_iterator&, const basic_iterator&) noexcept = default;
	};

public:
	using value_type = T;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using reference = T&;
	using const_reference = const T&;
	using pointer = T*;
	using const_pointer = const T*;
	using iterator = basic_iterator<false>;
	using const_iterator = basic_iterator<true>;
	using reverse_iterator = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

	static constexpr size_type block_capacity = block_capacity_;
	static constexpr size_type value_bytes_per_block = sizeof(T) * block_capacity_;
	static constexpr size_type offset_bytes_per_block = 2 * block_capacity_;
	static constexpr size_type control_block_bytes = sizeof(control_block);

	buffered_block_list() noexcept
	{
		root_.next = &root_;
		root_.previous = &root_;
	}

	~buffered_block_list()
	{
		clear();
	}

	buffered_block_list(const buffered_block_list&) = delete;
	buffered_block_list& operator=(const buffered_block_list&) = delete;
	buffered_block_list(buffered_block_list&&) = delete;
	buffered_block_list& operator=(buffered_block_list&&) = delete;

	[[nodiscard]] bool empty() const noexcept
	{
		return size_ == 0;
	}

	[[nodiscard]] size_type size() const noexcept
	{
		return size_;
	}

	[[nodiscard]] size_type block_count() const noexcept
	{
		return block_count_;
	}

	[[nodiscard]] iterator begin() noexcept
	{
		return iterator(root_.next, root_.next->live == 0 ? 0 : root_.next->first);
	}

	[[nodiscard]] const_iterator begin() const noexcept
	{
		return cbegin();
	}

	[[nodiscard]] const_iterator cbegin() const noexcept
	{
		return const_iterator(root_.next, root_.next->live == 0 ? 0 : root_.next->first);
	}

	[[nodiscard]] iterator end() noexcept
	{
		return iterator(&root_, 0);
	}

	[[nodiscard]] const_iterator end() const noexcept
	{
		return cend();
	}

	[[nodiscard]] const_iterator cend() const noexcept
	{
		return const_iterator(const_cast<block_link*>(&root_), 0);
	}

	[[nodiscard]] reverse_iterator rbegin() noexcept
	{
		return reverse_iterator(end());
	}

	[[nodiscard]] const_reverse_iterator rbegin() const noexcept
	{
		return crbegin();
	}

	[[nodiscard]] const_reverse_iterator crbegin() const noexcept
	{
		return const_reverse_iterator(cend());
	}

	[[nodiscard]] reverse_iterator rend() noexcept
	{
		return reverse_iterator(begin());
	}

	[[nodiscard]] const_reverse_iterator rend() const noexcept
	{
		return crend();
	}

	[[nodiscard]] const_reverse_iterator crend() const noexcept
	{
		return const_reverse_iterator(cbegin());
	}

	[[nodiscard]] reference front() noexcept
	{
		return *begin();
	}

	[[nodiscard]] const_reference front() const noexcept
	{
		return *cbegin();
	}

	[[nodiscard]] reference back() noexcept
	{
		auto result = end();
		--result;
		return *result;
	}

	[[nodiscard]] const_reference back() const noexcept
	{
		auto result = cend();
		--result;
		return *result;
	}

	template<typename... Args>
	reference emplace_back(Args&&... args)
	{
		if (root_.previous == &root_)
		{
			auto [block, index] = create_block_before(&root_, 0, std::forward<Args>(args)...);
			return *block->values[index].value();
		}

		control_block* tail = as_block(root_.previous);
		if (can_append(*tail))
		{
			const std::uint8_t index = append_to(*tail, std::forward<Args>(args)...);
			return *tail->values[index].value();
		}

		auto [block, index] = create_block_before(&root_, 0, std::forward<Args>(args)...);
		return *block->values[index].value();
	}

	template<typename... Args>
	reference emplace_front(Args&&... args)
	{
		if (root_.next == &root_)
		{
			auto [block, index] = create_block_before(&root_, 0, std::forward<Args>(args)...);
			return *block->values[index].value();
		}

		control_block* head = as_block(root_.next);
		if (can_prepend(*head))
		{
			const std::uint8_t index = prepend_to(*head, std::forward<Args>(args)...);
			return *head->values[index].value();
		}

		auto [block, index] = create_block_before(root_.next, 0, std::forward<Args>(args)...);
		return *block->values[index].value();
	}

	void push_back(const T& value)
	{
		emplace_back(value);
	}

	void push_back(T&& value)
	{
		emplace_back(std::move(value));
	}

	void push_front(const T& value)
	{
		emplace_front(value);
	}

	void push_front(T&& value)
	{
		emplace_front(std::move(value));
	}

	// Inserts immediately before position when an existing physical gap can be
	// split. Endpoint insertion may allocate, but a middle insertion never does.
	template<typename... Args>
	[[nodiscard]] std::optional<iterator> try_emplace(
		const_iterator position,
		Args&&... args)
	{
		if (position.block_ == &root_)
		{
			emplace_back(std::forward<Args>(args)...);
			control_block* tail = as_block(root_.previous);
			return iterator(tail, tail->last);
		}

		control_block* current = as_block(position.block_);
		if (position.block_ == root_.next && position.index_ == current->first)
		{
			emplace_front(std::forward<Args>(args)...);
			control_block* head = as_block(root_.next);
			return iterator(head, head->first);
		}

		const std::uint8_t previous_offset =
			current->previous_offsets[position.index_];
		if (previous_offset > 1)
		{
			const std::uint8_t previous_index =
				retreat_index(position.index_, previous_offset);
			const std::uint8_t inserted_index = advance_index(previous_index, 1);
			construct_element(*current, inserted_index, std::forward<Args>(args)...);

			current->next_offsets[previous_index] = 1;
			current->previous_offsets[inserted_index] = 1;
			current->next_offsets[inserted_index] =
				static_cast<std::uint8_t>(previous_offset - 1);
			current->previous_offsets[position.index_] =
				static_cast<std::uint8_t>(previous_offset - 1);
			++current->live;
			++size_;
			return iterator(current, inserted_index);
		}

		if (previous_offset == 0)
		{
			control_block* previous = as_block(current->previous);
			if (can_append(*previous))
			{
				const std::uint8_t inserted_index =
					append_to(*previous, std::forward<Args>(args)...);
				return iterator(previous, inserted_index);
			}
			if (can_prepend(*current))
			{
				const std::uint8_t inserted_index =
					prepend_to(*current, std::forward<Args>(args)...);
				return iterator(current, inserted_index);
			}
		}

		return std::nullopt;
	}

	[[nodiscard]] std::optional<iterator> try_insert(
		const_iterator position,
		const T& value)
	{
		return try_emplace(position, value);
	}

	[[nodiscard]] std::optional<iterator> try_insert(
		const_iterator position,
		T&& value)
	{
		return try_emplace(position, std::move(value));
	}

	iterator erase(const_iterator position)
	{
		assert(position.block_ != &root_);
		control_block* block = as_block(position.block_);
		const std::uint8_t previous_offset = block->previous_offsets[position.index_];
		const std::uint8_t next_offset = block->next_offsets[position.index_];

		iterator following(position.block_, position.index_);
		++following;

		if (previous_offset != 0 && next_offset != 0)
		{
			const unsigned int combined =
				static_cast<unsigned int>(previous_offset) +
				static_cast<unsigned int>(next_offset);
			assert(combined < block_capacity_);
			const std::uint8_t combined_offset = static_cast<std::uint8_t>(combined);
			block->next_offsets[retreat_index(position.index_, previous_offset)] =
				combined_offset;
			block->previous_offsets[advance_index(position.index_, next_offset)] =
				combined_offset;
		}
		else if (previous_offset != 0)
		{
			const std::uint8_t previous_index =
				retreat_index(position.index_, previous_offset);
			block->next_offsets[previous_index] = 0;
			block->last = previous_index;
		}
		else if (next_offset != 0)
		{
			const std::uint8_t next_index = advance_index(position.index_, next_offset);
			block->previous_offsets[next_index] = 0;
			block->first = next_index;
		}

		std::destroy_at(block->values[position.index_].value());
		--block->live;
		--size_;
		if (block->live == 0)
		{
			unlink(block);
			blocks_.destroy(block);
			--block_count_;
		}
		return following;
	}

	void pop_back()
	{
		auto position = cend();
		--position;
		erase(position);
	}

	void pop_front()
	{
		erase(cbegin());
	}

	void clear()
	{
		block_link* link = root_.next;
		while (link != &root_)
		{
			control_block* block = as_block(link);
			block_link* next_block = block->next;
			std::uint8_t index = block->first;
			for (std::uint16_t remaining = block->live; remaining != 0; --remaining)
			{
				const std::uint8_t next_offset = block->next_offsets[index];
				std::destroy_at(block->values[index].value());
				if (next_offset != 0)
					index = advance_index(index, next_offset);
			}
			blocks_.destroy(block);
			link = next_block;
		}

		root_.next = &root_;
		root_.previous = &root_;
		size_ = 0;
		block_count_ = 0;
	}
};

} // namespace dixelu
