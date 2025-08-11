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

/* Returns hash ID, calls one of instantiated subroutines to
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
			auto static_string_buffer = static_cast<const char**>(ptr);
			*static_string_buffer = type_name;
			break;
		}
		case ALLOCATE:
		{
			auto static_data_buffer = static_cast<T**>(ptr);
			*static_data_buffer = alloc.allocate(1);
			break;
		}
		case DEALLOCATE:
		{
			alloc.deallocate(static_cast<T*>(ptr), 1);
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
	void* data;
	mf_sig mf;

public:
	template<class T>
	custom_head(T&& value) requires (!std::is_same_v<std::remove_cvref_t<T>, custom_head>) :
		data(nullptr),
		mf(mfunc<std::remove_cvref_t<T>>)
	{
		this->mf(&this->data, nullptr, ALLOCATE);
		data = new (static_cast<std::remove_cvref_t<T>*>(this->data)) std::remove_cvref_t<T>(std::forward<T>(value));
	}

	custom_head() :
		data(nullptr),
		mf(mfunc<std::nullptr_t>)
	{}

	custom_head(const custom_head& lhs) :
		data(nullptr),
		mf(lhs.mf)
	{
		lhs.mf(reinterpret_cast<void*>(&this->data), nullptr, ALLOCATE);
		lhs.mf(lhs.data, this->data, EMPLACE_COPY);
	}

	custom_head(custom_head&& lhs) noexcept :
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

	custom_head& operator=(const custom_head& lhs)
	{
		if (&lhs == this)
			return *this;

		this->reset(lhs.mf);

		lhs.mf(reinterpret_cast<void*>(&this->data), nullptr, ALLOCATE);
		lhs.mf(lhs.data, this->data, EMPLACE_COPY);

		return *this;
	}

	void swap(custom_head& lhs) noexcept
	{
		std::swap(lhs.data, this->data);
		std::swap(lhs.mf, this->mf);
	}

	custom_head& operator=(custom_head&& lhs) noexcept
	{
		this->reset(lhs.mf);
		this->swap(lhs);

		return *this;
	}

	[[nodiscard]] bool empty() const
	{
		return data == nullptr;
	}

	~custom_head()
	{
		if (this->mf)
			this->mf(data, nullptr, NONE);

		this->reset();
	}

	template<typename T>
	[[nodiscard]] bool is() const
	{
		auto hash_id = typeid(T).hash_code();
		return this->mf(nullptr, nullptr, NONE) == hash_id;
	}

	template<typename T>
	T get() const
	{
		if (this->is<T>())
			return *static_cast<const T*>(this->data);

		throw std::runtime_error("Bad get<T>() call");
	}

	template<typename T>
	T get(T default_value) const
	{
		if (this->is<T>())
			return *static_cast<const T*>(this->data);
		return default_value;
	}

	template<typename T>
	[[nodiscard]] const T& as() const
	{
		if (this->is<T>())
			return *static_cast<const T*>(data);

		throw std::runtime_error("Bad as<T>() const call");
	}

	template<typename T>
	[[nodiscard]] T& as()
	{
		if (this->is<T>())
			return *static_cast<T*>(data);

		throw std::runtime_error("Bad as<T>() const call");
	}

	[[nodiscard]] const char* get_type_name() const
	{
		auto res = "";
		this->mf(reinterpret_cast<void*>(&res), nullptr, COPY_TYPE_NAME);
		return res;
	}

	bool operator==(const custom_head& lhs) const
	{
		auto this_hash = this->mf(nullptr, nullptr, NONE);
		auto lhs_hash = lhs.mf(nullptr, nullptr, NONE);

		if (this_hash != lhs_hash)
			return false;

		return this->mf(this->data, lhs.data, COMPARE) != 0;
	}

	bool operator!=(const custom_head& lhs) const
	{
		return !(*this == lhs);
	}
};

template<typename T>
using possible_integral_alternative =
	std::conditional_t<std::is_integral_v<T> && (!std::is_same_v<bool, T>), uint64_t, T>;

template<typename T>
struct as_res
{
	using U = possible_integral_alternative<T>;
	constexpr static bool enable_type_punning = !std::is_same_v<U, T>;

	using type = std::conditional_t<enable_type_punning, T, U>;
};

template<typename T, typename Variant>
struct is_in_variant : std::false_type {};

template<typename T, typename... Ts>
struct is_in_variant<T, std::variant<Ts...>>
    : std::bool_constant<(std::is_same_v<std::remove_cvref_t<T>, std::remove_cvref_t<Ts>> || ...)> {};

template<typename T, typename Variant>
inline constexpr bool is_in_variant_v = is_in_variant<T, Variant>::value;

}

class mctx;

using mctx_array = std::vector<mctx>;
using mctx_object = std::map<std::string, mctx>;

class mctx
{
	using object = mctx_object;
	using array = mctx_array;
	using string = std::string;
	using custom = details::custom_head;

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

	mctx(custom v);

public:

	class value_iter;
	class key_value_iter;

	mctx();

	template<typename T>
	mctx(T&& v) requires std::is_integral_v<T> && (!std::is_same_v<bool, T>);

	mctx(bool v);
	mctx(double v);
	mctx(float v);

	mctx(const char* v);

	mctx(std::string v);
	mctx(std::string&& v);

	mctx(const mctx& v);
	mctx(mctx&& v) noexcept;

	mctx& operator=(const mctx&);
	mctx& operator=(mctx&&) noexcept;

	template<typename T>
	mctx(T v) requires (!std::is_fundamental_v<T>);

	[[nodiscard]] bool empty() const;

	template<typename T>
	[[nodiscard]] bool is() const;

	template<typename T>
	[[nodiscard]] T get() const;

	template<typename T>
	[[nodiscard]] T get(T default_value) const;

	template<typename T>
	[[nodiscard]] const typename details::as_res<T>::type& as() const;

	template<typename T>
	[[nodiscard]] typename details::as_res<T>::type& as();

	[[nodiscard]] bool is_none() const;

	[[nodiscard]] bool is_array() const;

	[[nodiscard]] bool is_object() const;

	[[nodiscard]] value_iter begin() const;

	[[nodiscard]] value_iter end() const;

	[[nodiscard]] value_iter rbegin() const;

	[[nodiscard]] value_iter rend() const;

	[[nodiscard]] value_iter find(const std::string& str) const;

	[[nodiscard]] key_value_iter kvfind(const std::string& str);

	[[nodiscard]] key_value_iter kvbegin() const;

	[[nodiscard]] key_value_iter kvend() const;

	value_iter erase(value_iter iter);

	// Container operations
	mctx& operator[](const std::string& key);

	mctx& operator[](size_t index);
	const mctx& operator[](size_t index) const;

	[[nodiscard]] const mctx& at(const std::string& key) const;
	[[nodiscard]] const mctx& at(size_t index) const;
	mctx& at(const std::string& key);
	mctx& at(size_t index);

	void push_back(mctx value);

	[[nodiscard]] size_t size() const;

	void clear();

	static mctx make_array();
	static mctx make_object();

private:
	value var;
};

class mctx::value_iter
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

	value_iter();
	value_iter(const value_iter&);
	value_iter(value_iter&&) noexcept;

	value_iter& operator=(const value_iter&);
	value_iter& operator=(value_iter&&) noexcept;

	explicit value_iter(iter_value v);

	value_iter& operator++();
	value_iter operator++(int);
	value_iter& operator--();
	value_iter operator--(int);

	const mctx& operator*() const;
	mctx& operator*();
	const mctx* operator->() const;
	mctx* operator->();

	bool operator==(const value_iter& lhs) const;
	bool operator!=(const value_iter& lhs) const;

	template<typename T>
	value_iter& __erase(T& target) requires std::is_same_v<T, mctx::array> || std::is_same_v<T, mctx::object>;

private:
	[[nodiscard]] mctx* access() const;
};

class mctx::key_value_iter
{
	using iter_value =
		std::variant<
			object::iterator,
			object::const_iterator,
			object::reverse_iterator,
			object::const_reverse_iterator>;

	iter_value val;

public:

	key_value_iter();
	key_value_iter(const key_value_iter&);
	key_value_iter(key_value_iter&&) noexcept;

	key_value_iter& operator=(const key_value_iter&);
	key_value_iter& operator=(key_value_iter&&) noexcept;

	key_value_iter(iter_value v);

	key_value_iter& operator++();
	key_value_iter operator++(int);
	key_value_iter& operator--();
	key_value_iter operator--(int);

	const object::value_type& operator*() const;
	object::value_type& operator*();
	const object::value_type* operator->() const;
	object::value_type* operator->();

	bool operator==(const key_value_iter& lhs) const;
	bool operator!=(const key_value_iter& lhs) const;

	key_value_iter& __erase(object& target);

private:
	[[nodiscard]] object::value_type* access() const;
};

inline mctx::mctx() = default;

template<typename T>
mctx::mctx(T&& v) requires std::is_integral_v<T> && (!std::is_same_v<bool, T>) :
	var(static_cast<uint64_t>( static_cast<typename details::try_unsigned<T>::type>(v))) { }

inline mctx::mctx(bool v) : var(v) {}
inline mctx::mctx(double v) : var(v) {}
inline mctx::mctx(float v) : var(v) {}

inline mctx::mctx(const char* v) : var(string(v)) {}

inline mctx::mctx(std::string v) : var(std::move(v)) {}
inline mctx::mctx(std::string&& v) : var(std::move(v)) {}

inline mctx::mctx(const mctx& v) = default;
inline mctx::mctx(mctx&& v) noexcept = default;

inline mctx& mctx::operator=(const mctx& v) = default;
inline mctx& mctx::operator=(mctx&& v) noexcept = default;

inline mctx::mctx(custom v) : var(std::move(v)) {}

inline bool mctx::empty() const
{
	return this->var.index() == 0 ||
		(std::holds_alternative<array>(this->var) && std::get<array>(this->var).empty()) ||
		(std::holds_alternative<string>(this->var) && std::get<string>(this->var).empty()) ||
		(std::holds_alternative<object>(this->var) && std::get<object>(this->var).empty()) ||
		(std::holds_alternative<custom>(this->var) && std::get<custom>(this->var).empty());
}

template <typename T>
mctx::mctx(T v) requires (!std::is_fundamental_v<T>) :
	var(custom{v}) {}


template<typename T>
bool mctx::is() const
{
	if (std::holds_alternative<custom>(this->var))
		return std::get<custom>(this->var).is<T>();

	using U = details::possible_integral_alternative<T>;
	if constexpr (details::is_in_variant_v<U, decltype(this->var)>)
		return std::holds_alternative<U>(this->var);

	return false;
}

template<typename T>
T mctx::get() const
{
	if (std::holds_alternative<custom>(this->var) && std::get<custom>(this->var).is<T>())
		return std::get<custom>(this->var).get<T>();

	using U = details::possible_integral_alternative<T>;
	if constexpr (details::is_in_variant_v<U, decltype(this->var)>)
		return static_cast<T>(std::get<U>(this->var));

	return T();
}

template<typename T>
T mctx::get(T default_value) const
{
	if (std::holds_alternative<custom>(this->var) && std::get<custom>(this->var).is<T>())
		return std::get<custom>(this->var).get<T>();

	using U = details::possible_integral_alternative<T>;
	if constexpr (!details::is_in_variant_v<U, decltype(this->var)>)
		return default_value;
	else
	{
		const auto* ptr = std::get_if<details::possible_integral_alternative<T>>(&this->var);
		if (ptr == nullptr)
			return default_value;

		return static_cast<T>(*ptr);
	}
}

template<typename T>
const typename details::as_res<T>::type& mctx::as() const
{
	if (std::holds_alternative<custom>(this->var) && std::get<custom>(this->var).is<T>())
		return std::get<custom>(this->var).as<T>();

	using U = details::possible_integral_alternative<T>;
	if constexpr (!details::is_in_variant_v<U, decltype(this->var)>)
		throw std::runtime_error("Bad as<T> const call ");
	else
	{
		const auto* ptr = std::get_if<typename details::as_res<T>::U>(&this->var);
		if (ptr == nullptr)
			throw std::runtime_error("Bad as<T> const call");

		if constexpr (details::as_res<T>::enable_type_punning)
			return *reinterpret_cast<const T*>(ptr);
		else
			return *ptr;
	}
}

template<typename T>
typename details::as_res<T>::type& mctx::as()
{
	if (std::holds_alternative<custom>(this->var) && std::get<custom>(this->var).is<T>())
		return std::get<custom>(this->var).as<T>();

	if constexpr (!details::is_in_variant_v<typename details::as_res<T>::U, decltype(this->var)>)
		throw std::runtime_error("Bad as<T> call");
	else
	{
		auto* ptr = std::get_if<typename details::as_res<T>::U>(&this->var);
		if (ptr == nullptr)
			throw std::runtime_error("Bad as<T> call");

		if constexpr (details::as_res<T>::enable_type_punning)
			return *reinterpret_cast<T*>(ptr);
		else
			return *ptr;
	}
}

inline bool mctx::is_none() const
{
	return this->var.index() == 0;
}

inline bool mctx::is_array() const
{
	return std::holds_alternative<array>(this->var);
}

inline bool mctx::is_object() const
{
	return std::holds_alternative<object>(this->var);
}

inline mctx::value_iter mctx::begin() const
{
	value_iter it;

	std::visit(details::overloaded{
		[&it](const array& a) { it = value_iter{a.begin()}; },
		[&it](const object& o) { it = value_iter{o.begin()}; },
		[](const auto &) -> void { }
	}, this->var);

	return it;
}

inline mctx::value_iter mctx::end() const
{
	value_iter it;

	std::visit(details::overloaded{
		[&it](const array& a) { it = value_iter{a.end()}; },
		[&it](const object& o) { it = value_iter{o.end()}; },
		[](const auto&) -> void {}
	}, this->var);

	return it;
}

inline mctx::value_iter mctx::rbegin() const
{
	value_iter it;

	std::visit(details::overloaded{
		[&it](const array& a) { it = value_iter{a.rbegin()}; },
		[&it](const object& o) { it = value_iter{o.rbegin()}; },
		[](const auto&) -> void {}
	}, this->var);

	return it;
}

inline mctx::value_iter mctx::rend() const
{
	value_iter it;

	std::visit(details::overloaded{
		[&it](const array& a) { it = value_iter{a.rend()}; },
		[&it](const object& o) { it = value_iter{o.rend()}; },
		[](const auto&) -> void {}
	}, this->var);

	return it;
}

inline mctx::value_iter mctx::find(const std::string& str) const
{
	value_iter it;

	std::visit(details::overloaded{
		[](const array&) { throw std::runtime_error("find is not defined for array"); },
		[&](const object& o) { it = value_iter{o.find(str)}; },
		[](const auto&) -> void {}
	}, this->var);

	return it;
}

inline mctx::key_value_iter mctx::kvfind(const std::string& str)
{
	key_value_iter it;

	std::visit(details::overloaded{
		[](const array&) { throw std::runtime_error("find is not defined for array"); },
		[&](const object& o) { it = key_value_iter{o.find(str)}; },
		[](const auto&) -> void {}
	}, this->var);

	return it;
}

inline mctx::key_value_iter mctx::kvbegin() const
{
	key_value_iter it;

	std::visit(details::overloaded{
		[](const array&) { throw std::runtime_error("find is not defined for array"); },
		[&](const object& o) { it = key_value_iter{o.begin()}; },
		[](const auto&) -> void {}
	}, this->var);

	return it;
}

inline mctx::key_value_iter mctx::kvend() const
{
	key_value_iter it;

	std::visit(details::overloaded{
		[](const array&) { throw std::runtime_error("find is not defined for array"); },
		[&](const object& o) { it = key_value_iter{o.end()}; },
		[](const auto&) -> void {}
	}, this->var);

	return it;
}

inline mctx::value_iter mctx::erase(value_iter iter)
{
	value_iter after;

	auto array_erase = [&](array& a) { after = iter.__erase(a); };
	auto object_erase = [&](object& o) { after = iter.__erase(o); };

	std::visit(details::overloaded{
		 array_erase, object_erase,
	[](const auto&) -> void {} }, this->var);

	return after;
}

inline mctx& mctx::operator[](const std::string& key)
{
	if (this->is_none())
		this->var = object{};

	return this->as<object>()[key];
}

inline mctx& mctx::operator[](size_t index) { return this->as<array>()[index]; }
inline const mctx& mctx::operator[](size_t index) const { return this->as<array>()[index]; }

inline const mctx& mctx::at(const std::string& key) const { return this->as<object>().at(key); }
inline const mctx& mctx::at(size_t index) const { return this->as<array>().at(index); }

inline mctx& mctx::at(const std::string& key) { return this->as<object>().at(key); }
inline mctx& mctx::at(size_t index) { return this->as<array>().at(index); }

inline void mctx::push_back(mctx value)
{
	if (this->is_none())
		this->var = array{};

	this->as<array>().push_back(std::move(value));
}

inline size_t mctx::size() const
{
	size_t size = 0;

	std::visit(details::overloaded{
		[&size](const array& a) { size = a.size(); },
		[&size](const object& o) { size = o.size(); },
		[](const std::monostate&) {},
		[](const auto&) -> void { throw std::runtime_error("size is not defined for scalars"); }
	}, this->var);

	return size;
}

inline void mctx::clear() { this->var = std::monostate{}; }

inline mctx mctx::make_array() { mctx m; m.var = array{}; return m; }
inline mctx mctx::make_object() { mctx m; m.var = object{}; return m; }

inline mctx::value_iter::value_iter() = default;
inline mctx::value_iter::value_iter(const value_iter&) = default;
inline mctx::value_iter::value_iter(value_iter&&) noexcept = default;

inline mctx::value_iter& mctx::value_iter::operator=(const value_iter&) = default;
inline mctx::value_iter& mctx::value_iter::operator=(value_iter&&) noexcept = default;

inline mctx::value_iter::value_iter(iter_value v) : val(std::move(v)) {}

inline mctx::value_iter& mctx::value_iter::operator++()
{
	std::visit([](auto&& arg) { ++arg; }, this->val);
	return *this;
}

inline mctx::value_iter mctx::value_iter::operator++(int)
{
	auto prev = *this;
	this->operator++();
	return prev;
}

inline mctx::value_iter& mctx::value_iter::operator--()
{
	std::visit([](auto&& arg) { --arg; }, this->val);
	return *this;
}

inline mctx::value_iter mctx::value_iter::operator--(int)
{
	auto prev = *this;
	this->operator--();
	return prev;
}

inline const mctx& mctx::value_iter::operator*() const { return *this->access(); }
inline mctx& mctx::value_iter::operator*() { return *this->access(); }
inline const mctx* mctx::value_iter::operator->() const { return this->access(); }
inline mctx* mctx::value_iter::operator->() { return this->access(); }

inline bool mctx::value_iter::operator==(const value_iter& lhs) const
{
	if (this->val.index() != lhs.val.index())
		return false;

	return this->val == lhs.val;
}

inline bool mctx::value_iter::operator!=(const value_iter& lhs) const { return !(*this == lhs); }

template<typename T>
mctx::value_iter& mctx::value_iter::__erase(T& target) requires std::is_same_v<T, mctx::array> || std::is_same_v<T, mctx::object>
{
	value_iter new_self;

	std::visit(details::overloaded{
		[&](typename T::iterator it) { new_self = value_iter{target.erase(it)}; },
		[&](typename T::const_iterator it) { new_self = value_iter{target.erase(it)}; },
		[](const auto&) { throw std::runtime_error("Bad erase call"); }
	}, this->val);

	return *this = std::move(new_self);
}

inline mctx* mctx::value_iter::access() const
{
	mctx* contained;

	std::visit(details::overloaded{
		[&contained](array::iterator it) { contained = &*it; },
		[&contained](array::const_iterator it) { contained = const_cast<mctx*>(&*it); },
		[&contained](const array::reverse_iterator& it) { contained = &*it; },
		[&contained](const array::const_reverse_iterator& it) { contained = const_cast<mctx*>(&*it); },
		[&contained](object::iterator it) { contained = &it->second; },
		[&contained](object::const_iterator it) { contained = const_cast<mctx*>(&it->second); },
		[&contained](const object::reverse_iterator& it) { contained = &it->second; },
		[&contained](const object::const_reverse_iterator& it) { contained = const_cast<mctx*>(&it->second); }
	}, this->val);

	return contained;
}

inline mctx::key_value_iter::key_value_iter() = default;
inline mctx::key_value_iter::key_value_iter(const key_value_iter&) = default;
inline mctx::key_value_iter::key_value_iter(key_value_iter&&) noexcept = default;

inline mctx::key_value_iter& mctx::key_value_iter::operator=(const key_value_iter&) = default;
inline mctx::key_value_iter& mctx::key_value_iter::operator=(key_value_iter&&) noexcept = default;

inline mctx::key_value_iter::key_value_iter(iter_value v) : val(std::move(v)) {}

inline mctx::key_value_iter& mctx::key_value_iter::operator++()
{
	std::visit([](auto&& arg) { ++arg; }, this->val);
	return *this;
}

inline mctx::key_value_iter mctx::key_value_iter::operator++(int)
{
	auto prev = *this;
	this->operator++();
	return prev;
}

inline mctx::key_value_iter& mctx::key_value_iter::operator--()
{
	std::visit([](auto&& arg) { --arg; }, this->val);
	return *this;
}

inline mctx::key_value_iter mctx::key_value_iter::operator--(int)
{
	auto prev = *this;
	this->operator--();
	return prev;
}

inline const mctx::object::value_type& mctx::key_value_iter::operator*() const { return *this->access(); }
inline mctx::object::value_type& mctx::key_value_iter::operator*() { return *this->access(); }
inline const mctx::object::value_type* mctx::key_value_iter::operator->() const { return this->access(); }
inline mctx::object::value_type* mctx::key_value_iter::operator->() { return this->access(); }

inline bool mctx::key_value_iter::operator==(const key_value_iter& lhs) const
{
	if (this->val.index() != lhs.val.index())
		return false;

	return this->val == lhs.val;
}

inline bool mctx::key_value_iter::operator!=(const key_value_iter& lhs) const { return !(*this == lhs); }

inline mctx::key_value_iter& mctx::key_value_iter::__erase(object& target)
{
	key_value_iter new_self;

	std::visit(details::overloaded{
		[&](object::iterator it) { new_self = key_value_iter{target.erase(it)}; },
		[&](object::const_iterator it) { new_self = key_value_iter{target.erase(it)}; },
		[](const auto&) { throw std::runtime_error("Bad erase call"); }
	}, this->val);

	return *this = std::move(new_self);
}

inline mctx::object::value_type* mctx::key_value_iter::access() const
{
	using accessed = object::value_type;
	accessed* contained;

	std::visit(details::overloaded{
		[&contained](object::iterator it) { contained = &*it; },
		[&contained](object::const_iterator it) { contained = const_cast<accessed*>(&*it); },
		[&contained](const object::reverse_iterator& it) { contained = &*it; },
		[&contained](const object::const_reverse_iterator& it) { contained = const_cast<accessed*>(&*it); }
	}, this->val);

	return contained;
}