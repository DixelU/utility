#include "mctx.h"

#include <format>

namespace dixelu
{

namespace details
{

template<>
size_t mfunc<std::nullptr_t>(void*, void*, adj_mf_ops)
{
	return typeid(std::nullptr_t).hash_code();
}

custom_head::custom_head():
	data(nullptr),
	mf(mfunc<std::nullptr_t>)
{}

custom_head::~custom_head()
{
	this->reset();
}

custom_head::custom_head(const custom_head& lhs):
	data(nullptr),
	mf(lhs.mf)
{
	lhs.mf(reinterpret_cast<void*>(&this->data), nullptr, ALLOCATE);
	lhs.mf(lhs.data, this->data, EMPLACE_COPY);
}

custom_head::custom_head(custom_head&& lhs) noexcept:
	data(lhs.data),
	mf(lhs.mf)
{
	lhs.data = nullptr;
	lhs.mf = mfunc<std::nullptr_t>;
}

void custom_head::reset(mf_sig new_mf_sig)
{
	if (this->data != nullptr)
	{
		this->mf(this->data, nullptr, DESTROY);
		this->mf(this->data, nullptr, DEALLOCATE);
		this->data = nullptr;
	}

	this->mf = new_mf_sig;
}

custom_head& custom_head::operator=(const custom_head& lhs)
{
	if (&lhs == this)
		return *this;

	this->reset(lhs.mf);

	lhs.mf(reinterpret_cast<void*>(&this->data), nullptr, ALLOCATE);
	lhs.mf(lhs.data, this->data, EMPLACE_COPY);

	return *this;
}

void custom_head::swap(custom_head& lhs) noexcept
{
	std::swap(lhs.data, this->data);
	std::swap(lhs.mf, this->mf);
}

custom_head& custom_head::operator=(custom_head&& lhs) noexcept
{
	this->reset(lhs.mf);
	this->swap(lhs);

	return *this;
}

bool custom_head::empty() const
{
	return data == nullptr;
}


bool custom_head::operator==(const custom_head& lhs) const
{
	auto this_hash = this->mf(nullptr, nullptr, NONE);
	auto lhs_hash = lhs.mf(nullptr, nullptr, NONE);

	if (this_hash != lhs_hash)
		return false;

	return this->mf(this->data, lhs.data, COMPARE) != 0;
}

bool custom_head::operator!=(const custom_head& lhs) const
{
	return !(*this == lhs);
}

}

mctx::mctx() = default;

mctx::mctx(bool v) : var(v) {}
mctx::mctx(double v) : var(v) {}
mctx::mctx(float v) : var(v) {}

mctx::mctx(const char* v) : var(string(v)) {}

mctx::mctx(std::string v) : var(std::move(v)) {}

mctx::mctx(const mctx& v) = default;
mctx::mctx(mctx&& v) noexcept = default;

mctx& mctx::operator=(const mctx& v) = default;
mctx& mctx::operator=(mctx&& v) noexcept = default;

mctx::mctx(custom v) : var(std::move(v)) {}

template<>
bool mctx::is<mctx::custom>() const
{
	return std::holds_alternative<custom>(var);
}

bool mctx::empty() const
{
	return this->var.index() == 0 ||
		(std::holds_alternative<array>(this->var) && std::get<array>(this->var).empty()) ||
		(std::holds_alternative<string>(this->var) && std::get<string>(this->var).empty()) ||
		(std::holds_alternative<object>(this->var) && std::get<object>(this->var).empty()) ||
		(std::holds_alternative<custom>(this->var) && std::get<custom>(this->var).empty());
}

bool mctx::is_none() const
{
	return this->var.index() == 0;
}

bool mctx::is_scalar() const
{
	return !this->is_none() && !this->is_object() && !this->is_array();
}

bool mctx::is_array() const
{
	return std::holds_alternative<array>(this->var);
}

bool mctx::is_object() const
{
	return std::holds_alternative<object>(this->var);
}

mctx::value_iter mctx::begin() const
{
	value_iter it;

	std::visit(details::overloaded{
		[&it](const array& a) { it = value_iter{a.begin()}; },
		[&it](const object& o) { it = value_iter{o.begin()}; },
		[](const auto &) -> void { }
	}, this->var);

	return it;
}

mctx::value_iter mctx::end() const
{
	value_iter it;

	std::visit(details::overloaded{
		[&it](const array& a) { it = value_iter{a.end()}; },
		[&it](const object& o) { it = value_iter{o.end()}; },
		[](const auto&) -> void {}
	}, this->var);

	return it;
}

mctx::value_iter mctx::rbegin() const
{
	value_iter it;

	std::visit(details::overloaded{
		[&it](const array& a) { it = value_iter{a.rbegin()}; },
		[&it](const object& o) { it = value_iter{o.rbegin()}; },
		[](const auto&) -> void {}
	}, this->var);

	return it;
}

mctx::value_iter mctx::rend() const
{
	value_iter it;

	std::visit(details::overloaded{
		[&it](const array& a) { it = value_iter{a.rend()}; },
		[&it](const object& o) { it = value_iter{o.rend()}; },
		[](const auto&) -> void {}
	}, this->var);

	return it;
}

mctx::value_iter mctx::find(const std::string& str) const
{
	value_iter it;

	std::visit(details::overloaded{
		[](const array&) { throw std::runtime_error("find is not defined for array"); },
		[&](const object& o) { it = value_iter{o.find(str)}; },
		[](const auto&) -> void {}
	}, this->var);

	return it;
}

mctx::key_value_iter mctx::kvfind(const std::string& str)
{
	key_value_iter it;

	std::visit(details::overloaded{
		[](const array&) { throw std::runtime_error("find is not defined for array"); },
		[&](const object& o) { it = key_value_iter{o.find(str)}; },
		[](const auto&) -> void {}
	}, this->var);

	return it;
}

mctx::key_value_iter mctx::kvbegin() const
{
	key_value_iter it;

	std::visit(details::overloaded{
		[](const array&) { throw std::runtime_error("find is not defined for array"); },
		[&](const object& o) { it = key_value_iter{o.begin()}; },
		[](const auto&) -> void {}
	}, this->var);

	return it;
}

mctx::key_value_iter mctx::kvend() const
{
	key_value_iter it;

	std::visit(details::overloaded{
		[](const array&) { throw std::runtime_error("find is not defined for array"); },
		[&](const object& o) { it = key_value_iter{o.end()}; },
		[](const auto&) -> void {}
	}, this->var);

	return it;
}

mctx::value_iter mctx::erase(value_iter iter)
{
	value_iter after;

	auto array_erase = [&](array& a) { after = iter.__erase(a); };
	auto object_erase = [&](object& o) { after = iter.__erase(o); };

	std::visit(details::overloaded{
		 array_erase, object_erase,
	[](const auto&) -> void {} }, this->var);

	return after;
}


mctx& mctx::operator[](const std::string& key)
{
	if (this->is_none())
		this->var = object{};

	return this->as<object>()[key];
}

mctx& mctx::operator[](size_t index) { return this->as<array>()[index]; }
const mctx& mctx::operator[](size_t index) const { return this->as<array>()[index]; }

const mctx& mctx::at(const std::string& key) const { return this->as<object>().at(key); }
const mctx& mctx::at(size_t index) const { return this->as<array>().at(index); }

mctx& mctx::at(const std::string& key) { return this->as<object>().at(key); }
mctx& mctx::at(size_t index) { return this->as<array>().at(index); }

void mctx::push_back(mctx val_t)
{
	if (this->is_none())
		this->var = array{};

	this->as<array>().push_back(std::move(val_t));
}

size_t mctx::size() const
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

void mctx::clear() { this->var = std::monostate{}; }

mctx mctx::make_array() { mctx m; m.var = array{}; return m; }
mctx mctx::make_object() { mctx m; m.var = object{}; return m; }

bool mctx::operator==(const mctx& v) const { return v.var == this->var; }

mctx::value_iter::value_iter() = default;
mctx::value_iter::value_iter(const value_iter&) = default;
mctx::value_iter::value_iter(value_iter&&) noexcept = default;

mctx::value_iter& mctx::value_iter::operator=(const value_iter&) = default;
mctx::value_iter& mctx::value_iter::operator=(value_iter&&) noexcept = default;

mctx::value_iter::value_iter(iter_value v) : val(std::move(v)) {}

mctx::value_iter& mctx::value_iter::operator++()
{
	std::visit([](auto&& arg) { ++arg; }, this->val);
	return *this;
}

mctx::value_iter mctx::value_iter::operator++(int)
{
	auto prev = *this;
	this->operator++();
	return prev;
}

mctx::value_iter& mctx::value_iter::operator--()
{
	std::visit([](auto&& arg) { --arg; }, this->val);
	return *this;
}

mctx::value_iter mctx::value_iter::operator--(int)
{
	auto prev = *this;
	this->operator--();
	return prev;
}

const mctx& mctx::value_iter::operator*() const { return *this->access(); }
mctx& mctx::value_iter::operator*() { return *this->access(); }
const mctx* mctx::value_iter::operator->() const { return this->access(); }
mctx* mctx::value_iter::operator->() { return this->access(); }

bool mctx::value_iter::operator==(const value_iter& lhs) const
{
	if (this->val.index() != lhs.val.index())
		return false;

	return this->val == lhs.val;
}

bool mctx::value_iter::operator!=(const value_iter& lhs) const { return !(*this == lhs); }

mctx* mctx::value_iter::access() const
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

mctx::key_value_iter::key_value_iter() = default;
mctx::key_value_iter::key_value_iter(const key_value_iter&) = default;
mctx::key_value_iter::key_value_iter(key_value_iter&&) noexcept = default;

mctx::key_value_iter& mctx::key_value_iter::operator=(const key_value_iter&) = default;
mctx::key_value_iter& mctx::key_value_iter::operator=(key_value_iter&&) noexcept = default;

mctx::key_value_iter::key_value_iter(iter_value v) : val(std::move(v)) {}

mctx::key_value_iter& mctx::key_value_iter::operator++()
{
	std::visit([](auto&& arg) { ++arg; }, this->val);
	return *this;
}

mctx::key_value_iter mctx::key_value_iter::operator++(int)
{
	auto prev = *this;
	this->operator++();
	return prev;
}

mctx::key_value_iter& mctx::key_value_iter::operator--()
{
	std::visit([](auto&& arg) { --arg; }, this->val);
	return *this;
}

mctx::key_value_iter mctx::key_value_iter::operator--(int)
{
	auto prev = *this;
	this->operator--();
	return prev;
}

const mctx::object::value_type& mctx::key_value_iter::operator*() const { return *this->access(); }
mctx::object::value_type& mctx::key_value_iter::operator*() { return *this->access(); }
const mctx::object::value_type* mctx::key_value_iter::operator->() const { return this->access(); }
mctx::object::value_type* mctx::key_value_iter::operator->() { return this->access(); }

bool mctx::key_value_iter::operator==(const key_value_iter& lhs) const
{
	if (this->val.index() != lhs.val.index())
		return false;

	return this->val == lhs.val;
}

bool mctx::key_value_iter::operator!=(const key_value_iter& lhs) const { return !(*this == lhs); }

template<>
std::string mctx::get_as<std::string>(std::string result) const
{
	std::visit(details::overloaded{
		[&](const std::monostate&) { /* default_value */ },
		[&](bool v) { result = v ? "true" : "false"; },
		[&](uint64_t v) { result = std::to_string(v); },
		[&](float v) { result = std::format("{}", v); },
		[&](double v) { result = std::format("{}", v); },
		[&](const std::string& v) { result = v; },
		[&](const custom& c)
		{
			if (c.is<std::string>())
				result = c.get<std::string>();
			else
				result = c.get_type_name();
		},
		[&](const array& a) { result = "[array]"; },
		[&](const object& o) { result = "{object}"; }
	}, this->var);

	return result;
}

mctx::key_value_iter& mctx::key_value_iter::__erase(object& target)
{
	key_value_iter new_self;

	std::visit(details::overloaded{
		[&](object::iterator it) { new_self = key_value_iter{target.erase(it)}; },
		[&](object::const_iterator it) { new_self = key_value_iter{target.erase(it)}; },
		[](const auto&) { throw std::runtime_error("Bad erase call"); }
	}, this->val);

	return *this = std::move(new_self);
}

mctx::object::value_type* mctx::key_value_iter::access() const
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

}