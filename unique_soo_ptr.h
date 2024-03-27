#ifndef DIXELU_UNIQUE_SOO_PTR_H
#define DIXELU_UNIQUE_SOO_PTR_H

#include <memory>
#include <stdexcept>

#include <type_traits>
#include <cstdlib>

namespace dixelu
{

namespace __spec_ptr
{

// clang and many other compilers don't have aligned_malloc in them (only after c++17)
// taken from: https://stackoverflow.com/a/51937253
struct __aligned_malloc_impl_result
{
	void* p;
	size_t offset;
};

// also is not widely supported function
inline static void* __align_impl(
	std::size_t alignment,
	std::size_t size,
	void*& ptr,
	std::size_t& space)
{
	std::uintptr_t pn = reinterpret_cast<std::uintptr_t>(ptr);
	std::uintptr_t aligned = (pn + alignment - 1) & -alignment;
	std::size_t padding = aligned - pn;
	if (space < size + padding) return nullptr;
	space -= padding;
	return ptr = reinterpret_cast<void*>(aligned);
}

inline static __aligned_malloc_impl_result __aligned_malloc_impl(
	size_t alignment,
	size_t required_bytes)
{
	size_t offset = alignment - 1;
	size_t size = required_bytes + offset;
	void* ptr = malloc(size);

	if (!ptr)
		return { nullptr, 0 };

	auto aligned_ptr = __align_impl(alignment, required_bytes, ptr, size);

	return { aligned_ptr, reinterpret_cast<size_t>(aligned_ptr) - reinterpret_cast<size_t>(ptr) };
}

struct operands_provider
{
	template<typename T>
	static void __explicit_destroy(void* ptr) { (*static_cast<T*>(ptr)).~T(); }

	static void __one_argument_nothing(void*) {}

	template<typename T>
	static void __explicit_placement_move(void* lhs, void* rhs)
	{
		new (lhs) T(std::move(*static_cast<T*>(rhs)));
	}

	template<typename T>
	static void __explicit_placement_copy(void* lhs, void* rhs)
	{
		new (lhs) T(*static_cast<T*>(rhs));
	}

	static void __two_arguments_nothing(void*, void*) {}
};

struct operands_
{
	void (*_destroy)(void*) = operands_provider::__one_argument_nothing;
	void (*_placement_move)(void*, void*) = operands_provider::__two_arguments_nothing;
	//void (*_placement_copy)(void*, void*) = operands_provider::__two_arguments_nothing;
};

template<typename base_type, size_t buffer_size>
struct type_erased_soo_ptr
{
	// size of object is of size of cache line?
	constexpr static size_t expected_size = 64;
	constexpr static size_t possible_buffer_size =
		expected_size -
		sizeof(base_type*) - sizeof(operands_) -
		sizeof(uint16_t) - sizeof(uint8_t) * 2;

private:
	base_type* _ptr;
	operands_ _opfuncs;
	uint16_t _size;
	uint8_t _ptr_offset;
	uint8_t _aligment;
	uint8_t _buf[buffer_size];
public:

	template<typename U>
	explicit type_erased_soo_ptr(U&& rhs)
	{
		static_assert(sizeof(U) < (1 << 16),
					  "Stored object size must be less than 65kb");
		static_assert(alignof(U) < (1 << 8),
					  "Stored object must have aligment of less than 256 bytes");

		__construct<U>(std::forward<U>(rhs));
	}

	~type_erased_soo_ptr()
	{
		__deallocate();
	}

	type_erased_soo_ptr(type_erased_soo_ptr&& rhs) noexcept
	{
		_opfuncs = rhs._opfuncs;
		if (rhs._ptr)
		{
			_size = rhs._size;
			_aligment = rhs._aligment;

			if (rhs.__is_soo_optimised())
			{
				auto ptr_data = __create_aligned();
				_ptr = static_cast<base_type*>(ptr_data.p);
				_ptr_offset = ptr_data.offset;
				_opfuncs._placement_move(_ptr, rhs._ptr);
			}
			else
			{
				_ptr = rhs._ptr;
				_ptr_offset = rhs._ptr_offset;
			}
			rhs._ptr = nullptr;
		}
		else
		{
			_size = 0;
			_aligment = 0;
			_ptr = nullptr;
			_ptr_offset = 0;
		}
	}

	type_erased_soo_ptr& operator=(type_erased_soo_ptr&& rhs) noexcept
	{
		__deallocate();

		_opfuncs = rhs._opfuncs;
		if (rhs._ptr)
		{
			_size = rhs._size;
			_aligment = rhs._aligment;

			if (rhs.__is_soo_optimised())
			{
				auto ptr_data = __create_aligned();
				_ptr = static_cast<base_type*>(ptr_data.p);
				_ptr_offset = ptr_data.offset;
				_opfuncs._placement_move(_ptr, rhs._ptr);
			}
			else
			{
				_ptr = rhs._ptr;
				_ptr_offset = rhs._ptr_offset;
			}
			rhs._ptr = nullptr;
		}
		else
		{
			_size = 0;
			_aligment = 0;
			_ptr_offset = 0;
		}

		return *this;
	}

	type_erased_soo_ptr(const type_erased_soo_ptr&) = delete;
	type_erased_soo_ptr& operator=(const type_erased_soo_ptr& rhs) = delete;

private:
	template<typename U>
	inline void __construct(
		typename std::enable_if<!std::is_same<U, std::nullptr_t >::value, U>::type&& rhs)
	{
		_aligment = alignof(U);
		_size = sizeof(U);

		auto aligned_ptr_data = __create_aligned();

		_ptr_offset = aligned_ptr_data.offset;
		_opfuncs._destroy = operands_provider::__explicit_destroy<U>;
		_opfuncs._placement_move = operands_provider::__explicit_placement_move<U>;
		//_opfuncs._placement_copy = operands_provider::__explicit_placement_copy<U>;

		_ptr = new (aligned_ptr_data.p) U(std::forward<U>(rhs));
	}

	template<typename U>
	inline void __construct(
		typename std::enable_if<std::is_same<U, std::nullptr_t >::value, U>::type&& rhs)
	{
		_aligment = 0;
		_size = 0;
		_ptr_offset = 0;
		_ptr = nullptr;
		_opfuncs = operands_{};
	}

	template<bool with_throw = true, bool force_no_alloc = false>
	inline __aligned_malloc_impl_result __create_aligned()
	{
		size_t aligned_buffer_size = buffer_size;
		void* buffer_pointer = _buf;

		size_t offset = 0;
		auto aligned_soo_ptr =
			__align_impl(_aligment, _size, buffer_pointer, aligned_buffer_size);
		auto aligned_ptr = aligned_soo_ptr;
		if (!aligned_ptr && !force_no_alloc)
		{
			auto aligned_result = __aligned_malloc_impl(_aligment, _size);
			aligned_ptr = aligned_result.p;
			offset = aligned_result.offset;
		}

		if (with_throw && !aligned_ptr)
			throw std::bad_alloc();

		return { aligned_ptr, offset };
	}
public:

	template<typename U = base_type>
	inline U* get() noexcept
	{
		return static_cast<U*>(_ptr);
	}

	template<typename U = base_type>
	inline U* dynamic_get() noexcept
	{
		return dynamic_cast<U*>(_ptr);
	}

	template<typename U = base_type>
	inline const U* get() const noexcept
	{
		return static_cast<const U*>(_ptr);
	}

	template<typename U = base_type>
	inline const U* dynamic_get() const noexcept
	{
		return dynamic_cast<U*>(_ptr);
	}

	// this ptr must be created bv
	template<typename U = base_type, bool with_optimisation = true>
	inline void reset(U* ptr = nullptr)
	{
		__deallocate();

		if (!ptr)
			return;

		_aligment = alignof(U);
		_size = sizeof(U);

		_opfuncs._destroy = operands_provider::__explicit_destroy<U>;
		_opfuncs._placement_move = operands_provider::__explicit_placement_move<U>;
		//_opfuncs._placement_copy = operands_provider::__explicit_placement_copy<U>;

		if (with_optimisation && __can_be_soo_optimised<U>())
		{
			auto aligned_ptr = __create_aligned<true, true>();
			_opfuncs._placement_move(aligned_ptr.p, ptr);
			delete ptr;

			_ptr = static_cast<base_type*>(aligned_ptr.p);
			_ptr_offset = aligned_ptr.offset;
		}
		else
		{
			_ptr = ptr;
			_ptr_offset = 0;
		}
	}

	template<typename U>
	inline void reset(
		typename std::enable_if<
			(!std::is_same<std::nullptr_t, U>::value &&
			 !std::is_pointer<U>::value), U>::type&& rhs)
	{
		U unconditionally_moved_data = std::move(rhs);
		__deallocate();

		_aligment = alignof(U);
		_size = sizeof(U);

		_opfuncs._destroy = operands_provider::__explicit_destroy<U>;
		_opfuncs._placement_move = operands_provider::__explicit_placement_move<U>;
		//_opfuncs._placement_copy = operands_provider::__explicit_placement_copy<U>;

		__aligned_malloc_impl_result aligned_ptr = __create_aligned();
		_ptr_offset = aligned_ptr.offset;
		_ptr = static_cast<U*>(aligned_ptr.p);
		_opfuncs._placement_move(aligned_ptr.p, &unconditionally_moved_data);
	}

	inline base_type& operator*() { return *_ptr; }
	inline const base_type& operator*() const { return *_ptr; }
	inline base_type* operator->() { return _ptr; }
	inline const base_type* operator->() const { return _ptr; }

private:
	inline void __deallocate()
	{
		if (!_ptr)
			return;

		_opfuncs._destroy(_ptr);

		if (!__is_soo_optimised())
			::free(_ptr - static_cast<size_t>(_ptr_offset));

		_ptr = nullptr;
	}

	template<typename U>
	inline static bool __can_be_soo_optimised()
	{
		constexpr size_t size = sizeof(U);
		constexpr size_t aligment = alignof(U);
		return buffer_size >= size && aligment <= alignof(type_erased_soo_ptr);
	}
public:

	inline bool __is_soo_optimised() const
	{
		return
			reinterpret_cast<uint8_t*>(_ptr) >= _buf &&
			reinterpret_cast<uint8_t*>(_ptr) < (_buf + buffer_size);
	}

	inline operator bool() const
	{
		return static_cast<bool>(get());
	}
};

} // __spec_ptr

template<typename base_type, size_t buffer_size>
using unique_soo_ptr = __spec_ptr::type_erased_soo_ptr<base_type, buffer_size>;

} // dixelu

#endif // DIXELU_UNIQUE_SOO_PTR_H
