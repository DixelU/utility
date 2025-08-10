#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <variant>
#include <vector>
#include <utility>

namespace details
{

template<typename, typename, typename = void>
struct is_bool_comparable : std::false_type {};

template<typename T, typename U>
struct is_bool_comparable<T, U, std::enable_if_t<std::is_convertible_v<decltype(std::declval<T>() == std::declval<U>()), bool>>> : std::true_type {};

template<typename, bool>
struct __try_unsigned {};

template<typename T>
struct __try_unsigned<T, true>
{
	using type = std::make_unsigned_t<T>;
};

template<typename T>
struct __try_unsigned<T, false>
{
	using type = T;
};

template<typename T>
struct try_unsigned
{
	using type = typename
		__try_unsigned<
			T,
			std::numeric_limits<T>::is_integer&& std::numeric_limits<T>::is_signed
		>::type;
};

template<typename, bool>
struct __try_signed {};

template<typename T>
struct __try_signed<T, true>
{
	using type = std::make_signed_t<T>;
};

template<typename T>
struct __try_signed<T, false>
{
	using type = T;
};

template<typename T>
struct try_signed
{
	using type = typename
		__try_signed<
			T,
			std::numeric_limits<T>::is_integer&& std::numeric_limits<T>::is_signed
		>::type;
};

template<typename, bool>
struct __opposite_sign_type {};

template<typename T>
struct __opposite_sign_type<T, true>
{
	using type = typename
		__try_unsigned<
			T,
			std::numeric_limits<T>::is_integer&& std::numeric_limits<T>::is_signed
		>::type;
};

template<typename T>
struct __opposite_sign_type<T, false>
{
	using type = typename
		__try_signed<
			T,
			std::numeric_limits<T>::is_integer&& std::numeric_limits<T>::is_signed
		>::type;
};

template<typename T>
struct opposite_sign_type
{
	using type = typename
		__opposite_sign_type<T, std::is_signed_v<T>>::type;
};

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

template <typename T>
static bool compare(void* ptr, void* ptr2, std::enable_if_t<is_bool_comparable<T, T>::value, void>* = nullptr)
{
	const T& obj1 = *static_cast<T*>(ptr);
	const T& obj2 = *static_cast<T*>(ptr2);

	return (obj1 == obj2);
}

template <typename T>
static bool compare(void*, void*, std::enable_if_t<!is_bool_comparable<T, T>::value, void>* = nullptr)
{
	return false;
}

template<typename T>
void emplace_copy(void* from, void* to)
{
	new (to) T(*static_cast<T*>(from));
}

// ¯\_(ツ)_/¯
enum class adj_mf_ops
{
	NONE = 0,
	EMPLACE_COPY = 1,
	COMPARE = 2,
	COPY_TYPE_NAME = 3,
	ALLOCATE = 4,
	DEALLOCATE = 5,
};

using adj_mf_ops::NONE;
using adj_mf_ops::EMPLACE_COPY;
using adj_mf_ops::COMPARE;
using adj_mf_ops::COPY_TYPE_NAME;
using adj_mf_ops::ALLOCATE;
using adj_mf_ops::DEALLOCATE;

/* returns hash ID, calls one of instantiated subroutines to
 * perform a type-specific operation.
 * Possible to expand later on?
 */
template <typename T>
size_t mfunc(void* ptr, void* ptr2, adj_mf_ops adjacent_operation)
{
	static const std::type_info& type_info(typeid(T));
	static size_t hash_code{ type_info.hash_code() };
	static const char* type_name{ type_info.name() };
	static std::allocator<T> alloc{};

	switch (adjacent_operation)
	{
		case EMPLACE_COPY:
		{
			emplace_copy<T>(ptr, ptr2);
			break;
		}
		case COMPARE:
		{
			return compare<T>(ptr, ptr2);
		}
		case COPY_TYPE_NAME:
		{
			auto static_string_buffer = (const char**)ptr;
			*static_string_buffer = type_name;
			break;
		}
		case ALLOCATE:
		{
			auto static_data_buffer = (T**)ptr;
			*ptr = alloc.allocate(1);
			break;
		}
		case DEALLOCATE:
		{
			alloc.deallocate(ptr, 1);
			break;
		}
		default:
			break;
	}

	return hash_code;
}

template<>
inline size_t mfunc<std::nullptr_t>(void*, void*, adj_mf_ops)
{
	return typeid(std::nullptr_t).hash_code();
}

using mf_sig = size_t(*)(void* ptr, void* ptr2, adj_mf_ops adjacent_operation);

class custom_head final
{
	void *data;
	mf_sig mf;

public:
	template<class T>
	constexpr custom_head(T&& value):
		data(nullptr),
		mf(mfunc<T>)
	{
		this->mf(&this->data, nullptr, ALLOCATE);
		data = new (static_cast<T*>(this->data)) std::remove_cv_t<T>(std::forward<T>(value));
	}

	constexpr custom_head():
		data(nullptr),
		mf(mfunc<std::nullptr_t>)
	{}

	constexpr custom_head(const custom_head& lhs):
		data(nullptr),
		mf(lhs.mf)
	{
		lhs.mf(reinterpret_cast<void*>(&this->data), nullptr, ALLOCATE);
		lhs.mf(lhs.data, this->data, EMPLACE_COPY);
	}

	constexpr custom_head(custom_head&& lhs) noexcept :
		data(lhs.data),
		mf(lhs.mf)
	{
		lhs.data = nullptr;
		lhs.mf = mfunc<std::nullptr_t>;
	}

	void reset(mf_sig new_mf_sig = mfunc<std::nullptr_t>)
	{
		if (this->data != nullptr)
		{
			this->mf(this->data, nullptr, DEALLOCATE);
			this->data = nullptr;
		}

		this->mf = new_mf_sig;
	}

	constexpr custom_head& operator=(const custom_head& lhs)
	{
		this->reset(lhs.mf);
		
		lhs.mf(reinterpret_cast<void*>(&this->data), nullptr, ALLOCATE);
		lhs.mf(lhs.data, this->data, EMPLACE_COPY);

		return *this;
	}

	constexpr void swap(custom_head& lhs)
	{
		std::swap(lhs.data, this->data);
		std::swap(lhs.mf, this->mf);
	}

	constexpr custom_head& operator=(custom_head&& lhs) noexcept
	{
		this->reset(lhs.mf);
		this->swap(lhs);

		return *this;
	}

	[[nodiscard]] constexpr bool empty() const
	{
		return data == nullptr;
	}

	constexpr ~custom_head()
	{
		if (this->mf)
			this->mf(data, nullptr, NONE);
		
		this->reset();
	}

	template<typename T>
	[[nodiscard]] constexpr bool is() const
	{
		auto hash_id = typeid(T).hash_code();
		return this->mf(nullptr, nullptr, NONE) == hash_id;
	}

	template<typename T>
	constexpr T get() const
	{
		if (this->is<T>())
			return *static_cast<const T*>(this->data);

		throw std::runtime_error("Bad get<T>() call");
	}

	template<typename T>
	constexpr T get(T default_value) const
	{
		if (this->is<T>())
			return *static_cast<const T*>(this->data);
		return default_value;
	}

	template<typename T>
	[[nodiscard]] constexpr const T& as() const
	{
		if (this->is<T>())
			return *static_cast<const T*>(data);
		
		throw std::runtime_error("Bad as<T>() const call");
	}

	template<typename T>
	[[nodiscard]] constexpr T& as()
	{
		if (this->is<T>())
			return *static_cast<T*>(data);

		throw std::runtime_error("Bad as<T>() const call");
	}

	constexpr const char* get_type_name() const
	{
		auto res = "";
		this->mf(reinterpret_cast<void*>(&res), nullptr, COPY_TYPE_NAME);
		return res;
	}

	constexpr bool operator==(const custom_head& lhs) const
	{
		auto this_hash = this->mf(nullptr, nullptr, NONE);
		auto lhs_hash = lhs.mf(nullptr, nullptr, NONE);

		if (this_hash != lhs_hash)
			return false;

		return this->mf(this->data, lhs.data, COMPARE) != 0;
	}

	constexpr bool operator!=(const custom_head& lhs) const
	{
		return !( *this == lhs );
	}
};

}

class mctx
{
	using object = std::map<std::string, mctx>;
	using array = std::vector<mctx>;
	using string = std::string;
	using custom = details::custom_head;

	template<typename T>
	using possible_integral_alternative =
		std::conditional_t<std::is_integral_v<T> && !std::is_same_v<bool, T>, uint64_t, T>;
public:

	class value_iter
	{
		using iter_value = 
			std::variant<
				array::iterator,
				array::const_iterator,
				array::reverse_iterator,
				array::const_reverse_iterator,
				object::iterator,
				object::const_iterator,
				object::reverse_iterator,
				object::const_reverse_iterator>;

		iter_value val;

	public:

		value_iter() = default;
		value_iter(const value_iter&) = default;
		value_iter(value_iter&&) noexcept = default;

		value_iter& operator=(const value_iter&) = default;
		value_iter& operator=(value_iter&&) noexcept = default;

		explicit value_iter(iter_value v): val(std::move(v)) {}

		value_iter& operator++()
		{
			std::visit([](auto&& arg) { ++arg; }, this->val);
			return *this;
		}

		value_iter operator++(int)
		{
			auto prev = *this;
			this->operator++();
			return prev;
		}

		value_iter& operator--()
		{
			std::visit([](auto&& arg) { --arg; }, this->val);
			return *this;
		}

		value_iter operator--(int)
		{
			auto prev = *this;
			this->operator--();
			return prev;
		}

		const mctx& operator*() const { return *this->access(); }
		mctx& operator*() { return *this->access(); }
		const mctx* operator->() const { return this->access(); }
		mctx* operator->() { return this->access(); }

		bool operator==(const value_iter& lhs) const
		{
			if (this->val.index() != lhs.val.index())
				return false;

			return this->val == lhs.val;
		}

		bool operator!=(const value_iter& lhs) const { return !(*this == lhs); }

		template<typename T>
		value_iter& __erase(T& target) requires std::is_same_v<T, array> || std::is_same_v<T, object>
		{
			value_iter new_self;

			std::visit(details::overloaded{
				[&](T::iterator it) { new_self = value_iter{target.erase(it)}; },
				[&](T::const_iterator it) { new_self = value_iter{target.erase(it)}; },
				[](auto) { throw std::runtime_error("Bad erase call"); }
			}, this->val);

			//this->val = std::move(new_self.val);
			return *this;
		}

	private:
		mctx* access() const
		{
			mctx* contained;

			// const correctness violated so hard i'm a;sdkaksdlka
			std::visit(details::overloaded{
				[&contained](array::iterator it) { contained = const_cast<mctx*>(&*it);; },
				[&contained](array::const_iterator it) { contained = const_cast<mctx*>(&*it); },
				[&contained](array::reverse_iterator it) { contained = const_cast<mctx*>(&*it); },
				[&contained](array::const_reverse_iterator it) { contained = const_cast<mctx*>(&*it); },
				[&contained](object::iterator it) { contained = const_cast<mctx*>(&it->second);; },
				[&contained](object::const_iterator it) { contained = const_cast<mctx*>(&it->second); },
				[&contained](object::reverse_iterator it) { contained = const_cast<mctx*>(&it->second); },
				[&contained](object::const_reverse_iterator it) { contained = const_cast<mctx*>(&it->second); }
			}, this->val);

			return contained;
		}

	};

	class key_value_iter
	{
		using iter_value =
			std::variant<
				object::iterator,
				object::const_iterator,
				object::reverse_iterator,
				object::const_reverse_iterator>;

		iter_value val;

	public:

		key_value_iter() = default;
		key_value_iter(const key_value_iter&) = default;
		key_value_iter(key_value_iter&&) noexcept = default;

		key_value_iter& operator=(const key_value_iter&) = default;
		key_value_iter& operator=(key_value_iter&&) noexcept = default;

		key_value_iter(iter_value v) : val(std::move(v)) {}

		key_value_iter& operator++()
		{
			std::visit([](auto&& arg) { ++arg; }, this->val);
			return *this;
		}

		key_value_iter operator++(int)
		{
			auto prev = *this;
			this->operator++();
			return prev;
		}

		key_value_iter& operator--()
		{
			std::visit([](auto&& arg) { --arg; }, this->val);
			return *this;
		}

		key_value_iter operator--(int)
		{
			auto prev = *this;
			this->operator--();
			return prev;
		}

		const object::value_type& operator*() const { return *this->access(); }
		object::value_type& operator*() { return *this->access(); }
		const object::value_type* operator->() const { return this->access(); }
		object::value_type* operator->() { return this->access(); }

		bool operator==(const key_value_iter& lhs) const
		{
			if (this->val.index() != lhs.val.index())
				return false;

			return this->val == lhs.val;
		}

		bool operator!=(const value_iter& lhs) const { return !(*this == lhs); }

		key_value_iter& __erase(object& target)
		{
			key_value_iter new_self;

			std::visit(details::overloaded{
				[&](object::iterator it) { new_self = key_value_iter{target.erase(it)}; },
				[&](object::const_iterator it) { new_self = key_value_iter{target.erase(it)}; },
				[](auto) { throw std::runtime_error("Bad erase call"); }
			}, this->val);

			this->val = std::move(new_self.val);
			return *this;
		}

	private:
		[[nodiscard]] object::value_type* access() const
		{
			using acessed = object::value_type;
			acessed* contained;

			// const correctness violated so hard i'm a;sdkaksdlka
			std::visit(details::overloaded{
				[&contained](object::iterator it) { contained = const_cast<acessed*>(&*it); },
				[&contained](object::const_iterator it) { contained = const_cast<acessed*>(&*it); },
				[&contained](object::reverse_iterator it) { contained = const_cast<acessed*>(&*it); },
				[&contained](object::const_reverse_iterator it) { contained = const_cast<acessed*>(&*it); }
			}, this->val);

			return contained;
		}

	};

	using value =
		std::variant<
			std::monostate,
			bool,
			uint64_t,
			float, 
			double,
			string,
			custom,
			array,
			object
		>;

	constexpr mctx() = default;

	template<typename T>
	constexpr mctx(T&& v) requires std::is_integral_v<T> && (!std::is_same_v<bool, T>):
		var(static_cast<uint64_t>( static_cast<typename details::try_unsigned<T>::type>(v))) { }

	constexpr mctx(bool v) : var(v) {};
	constexpr mctx(double v): var(v) {};
	constexpr mctx(float v) : var(v) {};

	constexpr mctx(const char* v): var(string(v)) {}
	
	constexpr mctx(std::string v): var(std::move(v)) {}
	constexpr mctx(std::string&& v): var(std::move(v)) {}

	constexpr mctx(const mctx& v): var(v.var) {}
	constexpr mctx(mctx&& v): var(std::move(v.var)) {}

	constexpr mctx(custom v) : var(v) {};

	[[nodiscard]] bool empty() const
	{
		return this->var.index() == 0 || 
			(std::holds_alternative<array>(this->var) && std::get<array>(this->var).empty()) ||
			(std::holds_alternative<object>(this->var) && std::get<object>(this->var).empty()) ||
			(std::holds_alternative<custom>(this->var) && std::get<custom>(this->var).empty());
	}

	template<typename T>
	bool is() const
	{
		if (std::holds_alternative<custom>(this->var))
			return std::get<custom>(this->var).is<T>();
		
		return std::holds_alternative<possible_integral_alternative<T>>(this->var);
	}

	template<typename T>
	T get() const
	{
		if (std::holds_alternative<custom>(this->var) && std::get<custom>(this->var).is<T>())
			return std::get<custom>(this->var).get<T>();

		return static_cast<T>(std::get<possible_integral_alternative<T>>(this->var));
	}

	template<typename T>
	T get(T default_value) const
	{
		if (std::holds_alternative<custom>(this->var) && std::get<custom>(this->var).is<T>())
			return std::get<custom>(this->var).get<T>();

		const auto* ptr = std::get_if<possible_integral_alternative<T>>(&this->var);
		if (ptr == nullptr)
			return default_value;

		return static_cast<T>(*ptr);
	}

	template<typename T>
	[[nodiscard]] const T& as() const
	{
		using U = possible_integral_alternative<T>;
		constexpr bool enable_type_punning = !std::is_same_v<U, T>;

		if (std::holds_alternative<custom>(this->var) && std::get<custom>(this->var).is<T>())
			return std::get<custom>(this->var).as<T>();

		const auto* ptr = std::get_if<U>(&this->var);
		if (ptr == nullptr)
			throw std::runtime_error("Bad as<T> const call");

		if constexpr (enable_type_punning)
			return *reinterpret_cast<const T*>(ptr);

		return *ptr;
	}

	template<typename T>
	T& as()
	{
		using U = possible_integral_alternative<T>;
		constexpr bool enable_type_punning = !std::is_same_v<U, T>;

		if (std::holds_alternative<custom>(this->var) && std::get<custom>(this->var).is<T>())
			return std::get<custom>(this->var).as<T>();

		auto* ptr = std::get_if<U>(&this->var);
		if (ptr == nullptr)
			throw std::runtime_error("Bad as<T> const call");

		if constexpr (enable_type_punning)
			return *reinterpret_cast<T*>(ptr);

		return *ptr;
	}

	[[nodiscard]] bool is_none() const
	{
		return this->var.index() == 0;
	}

	[[nodiscard]] bool is_array() const
	{
		return std::holds_alternative<array>(this->var);
	}

	[[nodiscard]] bool is_object() const
	{
		return std::holds_alternative<object>(this->var);
	}

	[[nodiscard]] value_iter begin() const
	{
		value_iter it;
		
		std::visit(details::overloaded{
			[&it](const array& a) { it = value_iter{a.begin()}; },
			[&it](const object& o) { it = value_iter{o.begin()}; },
			[](const auto &) -> void { }
		}, this->var);

		return it;
	}

	[[nodiscard]] value_iter end() const
	{
		value_iter it;

		std::visit(details::overloaded{
			[&it](const array& a) { it = value_iter{a.end()}; },
			[&it](const object& o) { it = value_iter{o.end()}; },
			[](const auto&) -> void {}
		}, this->var);

		return it;
	}
	
	[[nodiscard]] value_iter rbegin() const
	{
		value_iter it;

		std::visit(details::overloaded{
			[&it](const array& a) { it = value_iter{a.rbegin()}; },
			[&it](const object& o) { it = value_iter{o.rbegin()}; },
			[](const auto&) -> void {}
		}, this->var);

		return it;
	}

	[[nodiscard]] value_iter rend() const
	{
		value_iter it;

		std::visit(details::overloaded{
			[&it](const array& a) { it = value_iter{a.rend()}; },
			[&it](const object& o) { it = value_iter{o.rend()}; },
			[](const auto&) -> void {}
		}, this->var);

		return it;
	}

	[[nodiscard]] value_iter find(const std::string& str) const
	{
		value_iter it;

		std::visit(details::overloaded{
			[](const array&) { throw std::runtime_error("find is not defined for array"); },
			[&](const object& o) { it = value_iter{o.find(str)}; },
			[](const auto&) -> void {}
		}, this->var);

		return it;
	}

	[[nodiscard]] key_value_iter kvfind(const std::string& str)
	{
		key_value_iter it;

		std::visit(details::overloaded{
			[](const array&) { throw std::runtime_error("find is not defined for array"); },
			[&](const object& o) { it = key_value_iter{o.find(str)}; },
			[](const auto&) -> void {}
		}, this->var);

		return it;
	}

	[[nodiscard]] key_value_iter kvbegin() const
	{
		key_value_iter it;

		std::visit(details::overloaded{
			[](const array&) { throw std::runtime_error("find is not defined for array"); },
			[&](const object& o) { it = key_value_iter{o.begin()}; },
			[](const auto&) -> void {}
		}, this->var);

		return it;
	}

	[[nodiscard]] key_value_iter kvend() const
	{
		key_value_iter it;

		std::visit(details::overloaded{
			[](const array&) { throw std::runtime_error("find is not defined for array"); },
			[&](const object& o) { it = key_value_iter{o.end()}; },
			[](const auto&) -> void {}
		}, this->var);

		return it;
	}

	value_iter erase(value_iter iter)
	{
		value_iter after;

		auto array_erase = [&](array& a) { after = iter.__erase(a); };
		auto object_erase = [&](object& a) { after = iter.__erase(a); };

		std::visit(details::overloaded{
			 array_erase, object_erase,
		[](const auto&) -> void {} }, this->var);

		return after;
	}

	// Container operations
	mctx& operator[](const std::string& key)
	{
		if (this->is_none())
			this->var = object{};

		return this->as<object>()[key];
	}

	mctx& operator[](size_t index) { return this->as<array>()[index]; }
	const mctx& operator[](size_t index) const { return this->as<array>()[index]; }

	[[nodiscard]] const mctx& at(const std::string& key) const { return this->as<object>().at(key); }
	[[nodiscard]] const mctx& at(size_t index) const { return this->as<array>().at(index); };

	void push_back(mctx value) { this->as<array>().push_back(std::move(value)); }
	
	[[nodiscard]] size_t size() const
	{
		size_t size = 0;

		std::visit(details::overloaded{
			[&size](const array& a) { size = a.size(); },
			[&size](const object& o) { size = o.size(); },
			[](const std::monostate&) { },
			[](const auto&) -> void { throw std::runtime_error("size is not defined for scalars"); }
		}, this->var);

		return size;
	}

	void clear() { this->var = std::monostate{}; }

private:
	value var;
};