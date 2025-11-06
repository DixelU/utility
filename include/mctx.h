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

namespace dixelu
{

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
	using type =
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
	using type =
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
	using type =
		__try_unsigned<
			T,
			std::numeric_limits<T>::is_integer&& std::numeric_limits<T>::is_signed
		>::type;
};

template<typename T>
struct __opposite_sign_type<T, false>
{
	using type =
		__try_signed<
			T,
			std::numeric_limits<T>::is_integer&& std::numeric_limits<T>::is_signed
		>::type;
};

template<typename T>
struct opposite_sign_type
{
	using type = __opposite_sign_type<T, std::is_signed_v<T>>::type;
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

enum class adj_mf_ops
{
	NONE = 0,
	EMPLACE_COPY = 1,
	COMPARE = 2,
	COPY_TYPE_NAME = 3,
	ALLOCATE = 4,
	DEALLOCATE = 5,
	DESTROY = 6
};

using adj_mf_ops::NONE;
using adj_mf_ops::EMPLACE_COPY;
using adj_mf_ops::COMPARE;
using adj_mf_ops::COPY_TYPE_NAME;
using adj_mf_ops::ALLOCATE;
using adj_mf_ops::DEALLOCATE;
using adj_mf_ops::DESTROY;

/* Returns hash ID, calls one of instantiated subroutines to
 * perform a type-specific operation.
 * Possible to expand later on?
 */
template <typename T>
size_t mfunc(void* ptr, void* ptr2, adj_mf_ops adjacent_operation)
{
	static const auto& type_info(typeid(T));
	static const size_t hash_code{ type_info.hash_code() };
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
		case DESTROY:
		{
			if constexpr (!std::is_trivially_destructible_v<T>)
				static_cast<T*>(ptr)->~T();
			break;
		}
		default:
			break;
	}

	return hash_code;
}

template<>
size_t mfunc<std::nullptr_t>(void*, void*, adj_mf_ops);

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

	custom_head();
	~custom_head();

	custom_head(const custom_head& lhs);
	custom_head(custom_head&& lhs) noexcept;

	void reset(mf_sig new_mf_sig = mfunc<std::nullptr_t>);
	void swap(custom_head& lhs) noexcept;

	custom_head& operator=(const custom_head& lhs);
	custom_head& operator=(custom_head&& lhs) noexcept;

	[[nodiscard]] bool empty() const;

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

	bool operator==(const custom_head& lhs) const;
	bool operator!=(const custom_head& lhs) const;
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

template<typename, typename>
struct is_in_variant : std::false_type {};

template<typename T, typename... Ts>
struct is_in_variant<T, std::variant<Ts...>> :
	std::bool_constant<(std::is_same_v<std::remove_cvref_t<T>, std::remove_cvref_t<Ts>> || ...)> {};

template<typename T, typename Variant>
constexpr bool is_in_variant_v = is_in_variant<T, Variant>::value;

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

	explicit mctx(custom v);

	template<typename T>
	static constexpr bool integral_constructor_req =
		std::is_integral_v<std::remove_cvref_t<T>> && (!std::is_same_v<bool, std::remove_cvref_t<T>>);

	template<typename T>
	static constexpr bool custom_type_reqs =
		!std::is_fundamental_v<std::remove_cvref_t<T>> &&
		!std::is_same_v<mctx, std::remove_cvref_t<T>> &&
		!details::is_in_variant_v<std::remove_cvref_t<T>, value>;

public:

	class value_iter;
	class key_value_iter;

	mctx();
	virtual ~mctx() = default;

	template<typename T>
	mctx(T&& v) requires integral_constructor_req<T>;

	mctx(bool v);
	mctx(double v);
	mctx(float v);

	mctx(const char* v);

	mctx(std::string v);

	mctx(const mctx& v);
	mctx(mctx&& v) noexcept;

	mctx& operator=(const mctx&);
	mctx& operator=(mctx&&) noexcept;

	template<typename T>
	mctx(T v) requires custom_type_reqs<T>;

	[[nodiscard]] bool empty() const;

	template<typename T>
	[[nodiscard]] bool is() const;

	template<typename T>
	[[nodiscard]] T get() const;

	template<typename T>
	[[nodiscard]] T get(T default_value) const;

	template<typename T>
	[[nodiscard]] const details::as_res<T>::type& as() const;

	template<typename T>
	[[nodiscard]] details::as_res<T>::type& as();

	template<typename T>
	[[nodiscard]] T get_as(T default_value = T()) const;

	template<typename T>
	[[nodiscard]] T get(const std::string& key, T default_value = T()) const;

	template<typename T>
	[[nodiscard]] T get_as(const std::string& key, T default_value = T()) const;

	[[nodiscard]] bool is_none() const;
	[[nodiscard]] bool is_scalar() const;
	[[nodiscard]] bool is_array() const;
	[[nodiscard]] bool is_object() const;

	[[nodiscard]] value_iter find(const std::string& str);
	[[nodiscard]] value_iter find(const std::string& str) const;
	[[nodiscard]] value_iter begin();
	[[nodiscard]] value_iter end();
	[[nodiscard]] value_iter begin() const;
	[[nodiscard]] value_iter end() const;

	[[nodiscard]] value_iter cbegin() const;
	[[nodiscard]] value_iter cend() const;

	[[nodiscard]] value_iter rbegin() const;
	[[nodiscard]] value_iter rend() const;

	[[nodiscard]] key_value_iter kvfind(const std::string& str);
	[[nodiscard]] key_value_iter kvfind(const std::string& str) const;
	[[nodiscard]] key_value_iter kvbegin() const;
	[[nodiscard]] key_value_iter kvend() const;
	[[nodiscard]] key_value_iter kvbegin();
	[[nodiscard]] key_value_iter kvend();

	value_iter erase(value_iter iter);
	key_value_iter erase(key_value_iter iter);

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

	bool operator==(const mctx& v) const;

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
	value_iter& __erase(T& target) requires std::is_same_v<T, array> || std::is_same_v<T, object>;

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

template<typename T>
mctx::mctx(T&& v) requires integral_constructor_req<T> :
	var(static_cast<uint64_t>( static_cast<details::try_unsigned<std::remove_cvref_t<T>>::type>(v))) { }

template <typename T>
mctx::mctx(T v) requires custom_type_reqs<T> :
	var(custom{v}) {}

template<>
bool mctx::is<mctx::custom>() const;

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
const details::as_res<T>::type& mctx::as() const
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
details::as_res<T>::type& mctx::as()
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

template<typename T>
T mctx::get_as(T default_value) const
{
	T value{ std::move(default_value) };

	std::visit(details::overloaded{
		[&](float v) { value = static_cast<T>(v); },
		[&](double v) { value = static_cast<T>(v); },
		[&](uint64_t v) { value = static_cast<T>(v); },
		[&](bool v) { value = static_cast<T>(v ? 1 : 0); },
		[&](const custom& c) { value = c.get<T>(default_value); },
		[&](const string& v)
		{
			if constexpr (std::is_floating_point_v<T>)
			{
				char* p = nullptr;
				double d = strtod(v.c_str(), &p);
				if (p != nullptr)
					return;

				value = static_cast<T>(std::stod(v));
			}
			else if constexpr (std::is_integral_v<T>)
			{
				char* p = nullptr;
				long long ll = strtoll(v.c_str(), &p, 10);
				if (p != nullptr)
					return;

				value = static_cast<T>(ll);
			}
			else if constexpr (std::is_same_v<T, std::string>)
				value = v;
		},
		[](const auto&) { /* Do nothing for empty or unsupported types */ }
	}, this->var);

	return value;
}

template <typename T>
T mctx::get(const std::string& key, T default_value) const
{
	auto iter = this->find(key);
	if (iter == this->end())
		return default_value;

	return this->get<T>(default_value);
}

template <typename T>
T mctx::get_as(const std::string& key, T default_value) const
{
	auto iter = this->find(key);
	if (iter == this->end())
		return default_value;

	return this->get_as<T>(default_value);
}

template<>
std::string mctx::get_as<std::string>(std::string default_value) const;

template<typename T>
mctx::value_iter& mctx::value_iter::__erase(T& target) requires std::is_same_v<T, array> || std::is_same_v<T, object>
{
	value_iter new_self;

	std::visit(details::overloaded{
		[&](typename T::iterator it) { new_self = value_iter{target.erase(it)}; },
		[&](typename T::const_iterator it) { new_self = value_iter{target.erase(it)}; },
		[](const auto&) { throw std::runtime_error("Bad erase call"); }
	}, this->val);

	return *this = std::move(new_self);
}

}
