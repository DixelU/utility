#include <chrono>
#include <ContextPath.h>
#include <iostream>

#include <MetaSDKUtils.h>

#include <regex>
#include <mctx_json.h>

dixelu::mctx& ContextPath::IntegerEntry::apply(dixelu::mctx& element) const
{ return element[_index]; }

const dixelu::mctx& ContextPath::IntegerEntry::apply(const dixelu::mctx& element) const
{ return element.at(_index); }

dixelu::mctx* ContextPath::IntegerEntry::try_apply(dixelu::mctx& element) const
{
	return element.is_array() && _index < element.size() ? &element.at(_index) : nullptr;
}

const dixelu::mctx* ContextPath::IntegerEntry::try_apply(const dixelu::mctx& element) const
{
	return element.is_array() && _index < element.size() ? &element.at(_index) : nullptr;
}

bool ContextPath::IntegerEntry::exists(const dixelu::mctx& element) const
{ return element.is_array() && _index < element.size(); }

ContextPath::SmallVector<const dixelu::mctx*> ContextPath::IntegerEntry::all(const dixelu::mctx& element) const
{
	SmallVector<const dixelu::mctx*> vector;
	if(element.is_array() && _index < element.size())
		vector.push_back(&element.at(_index));
	return vector;
}

bool ContextPath::IntegerEntry::remove(dixelu::mctx& element) const
{
	if(element.is_array() && _index < element.size())
	{
		element.erase(stringify());
		return true;
	}

	return false;
}

dixelu::mctx& ContextPath::StringEntry::apply(dixelu::mctx& element) const
{ return element[_key]; }

const dixelu::mctx& ContextPath::StringEntry::apply(const dixelu::mctx& element) const
{ return element.at(_key); }

dixelu::mctx* ContextPath::StringEntry::try_apply(dixelu::mctx& element) const
{
	if(!element.is_object())
		return nullptr;

	auto it = element.find(_key);
	if(it == element.end())
		return nullptr;
	return &*it;
}

const dixelu::mctx* ContextPath::StringEntry::try_apply(const dixelu::mctx& element) const
{
	if(!element.is_object())
		return nullptr;

	auto it = element.find(_key);
	if(it == element.end())
		return nullptr;
	return &*it;
}

ContextPath::SmallVector<const dixelu::mctx*> ContextPath::StringEntry::all(const dixelu::mctx& element) const
{
	SmallVector<const dixelu::mctx*> vector;
	auto it = element.find(_key);
	if(it != element.end())
		vector.push_back(&(*it));
	return vector;
}

bool ContextPath::StringEntry::exists(const dixelu::mctx& element) const
{
	return element.is_object() ?
		       element.find(_key) != element.end() : false;
}

bool ContextPath::StringEntry::remove(dixelu::mctx& element) const
{
	if(element.is_object() && element.find(_key) != element.end())
	{
		element.erase(_key);
		return true;
	}

	return false;
}

bool ContextPath::StringEntry::keyIsMalformed() const
{
	bool hadEscapeChar = false;
	for(const char& c: _key)
	{
		bool isEscapeChar = c == '\\';
		if(isEscapeChar)
		{
			hadEscapeChar = true;
			continue;
		}

		bool hasUnescapedString = c == '\"' && !hadEscapeChar;
		bool hasAndSymbolInside = c == '&';

		if(hasUnescapedString || hasAndSymbolInside)
			return true;
		hadEscapeChar = false;
	}
	return false;
}

dixelu::mctx& ContextPath::RootEntry::apply(dixelu::mctx& element) const
{
	dixelu::mctx* current = &element;
	if(_rootGetter)
	{
		auto possibleRoot = _rootGetter();
		if(possibleRoot)
			current = possibleRoot;
	}
	return *current;
}

const dixelu::mctx& ContextPath::RootEntry::apply(const dixelu::mctx& element) const
{
	const dixelu::mctx* current = &element;
	if(_rootGetter)
	{
		auto possibleRoot = _rootGetter();
		if(possibleRoot)
			current = possibleRoot;
	}
	return *current;
}

dixelu::mctx* ContextPath::RootEntry::try_apply(dixelu::mctx& element) const
{ return &apply(element); }

const dixelu::mctx* ContextPath::RootEntry::try_apply(const dixelu::mctx& element) const
{ return &apply(element); }

ContextPath::SmallVector<const dixelu::mctx*> ContextPath::RootEntry::all(const dixelu::mctx& element) const
{
	const dixelu::mctx* current = &element;
	if(_rootGetter)
	{
		auto possibleRoot = _rootGetter();
		if(possibleRoot)
			current = possibleRoot;
	}
	return {current};
}

ContextPath::EntryType ContextPath::RootEntry::type() const
{
	return EntryType::ROOT;
}

bool ContextPath::RootEntry::exists(const dixelu::mctx& element) const
{
	return true;
}

ContextPath::Entry* ContextPath::RootEntry::make_copy() const
{
	auto ptr = new RootEntry(__RootTag{});
	ptr->_rootGetter = _rootGetter;
	return ptr;
}

bool ContextPath::RootEntry::remove(dixelu::mctx& element) const
{
	dixelu::mctx* current = &element;
	if(_rootGetter)
		current = _rootGetter();
	current->clear();
	return true;
}

dixelu::mctx& ContextPath::PreviousEntry::apply(dixelu::mctx&) const
{ throw std::runtime_error("PreviousEntry cannot be used in ContextPath"); }

const dixelu::mctx& ContextPath::PreviousEntry::apply(const dixelu::mctx&) const
{ throw std::runtime_error("PreviousEntry cannot be used in ContextPath"); }

dixelu::mctx* ContextPath::PreviousEntry::try_apply(dixelu::mctx& element) const
{ return &apply(element); }

const dixelu::mctx* ContextPath::PreviousEntry::try_apply(const dixelu::mctx& element) const
{ return &apply(element); }

bool ContextPath::PreviousEntry::exists(const dixelu::mctx&) const
{ throw std::runtime_error("PreviousEntry cannot be used in ContextPath"); }

ContextPath::Entry* ContextPath::PreviousEntry::make_copy() const
{ return new PreviousEntry(__PreviousTag{}); }

ContextPath::SmallVector<const dixelu::mctx*> ContextPath::PreviousEntry::all(const dixelu::mctx& element) const
{ return {&element}; }

bool ContextPath::PreviousEntry::remove(dixelu::mctx& element) const
{ throw std::runtime_error("PreviousEntry cannot be used in ContextPath"); }

ContextPath::SubpathIsSetToValueEntry::SubpathIsSetToValueEntry(SubpathIsSetToValueEntry&& entry):
	SubpathIsSetToValueEntry(*entry._subpath, entry._entry.get())
{

}

ContextPath::SubpathIsSetToValueEntry::SubpathIsSetToValueEntry(const SubpathIsSetToValueEntry& entry):
	SubpathIsSetToValueEntry(*entry._subpath, entry._entry.get())
{}

ContextPath::Entry* ContextPath::SubpathIsSetToValueEntry::make_copy() const
{
	return new SubpathIsSetToValueEntry(*_subpath, _entry.get());
}

std::string ContextPath::SubpathIsSetToValueEntry::stringify() const
{
	return "*:{*/" + stringifyInternals() + "}";
}

std::string ContextPath::SubpathIsSetToValueEntry::stringifyInternals() const
{
	return _subpath->stringify() + "=" + _entry->stringify();
}

bool ContextPath::SubpathIsSetToValueEntry::__checkElementCompliance(const dixelu::mctx& element) const
{
	auto ptr = __applyImpl(element);
	if(!ptr)
		return false;

	if(auto stringEntry = dynamic_cast<StringEntry*>(_entry.get()))
	{
		if(ptr->is<std::string>())
			return ptr->as<std::string>() == stringEntry->__get();
		return false;
	}

	if(auto integerEntry = dynamic_cast<IntegerEntry*>(_entry.get()))
	{
		if(ptr->get_as<size_t>())
			return ptr->get_as<size_t>() == integerEntry->__get();
		return false;
	}

	if(dynamic_cast<UnidentifiedValue*>(_entry.get()))
		return true;

	return false;
}

ContextPath::SubpathIsSetToValueEntry::SubpathIsSetToValueEntry(ContextPath path, const Entry* entry):
	ConditionalEntry(std::bind(
		&SubpathIsSetToValueEntry::__checkElementCompliance,
		this,
		std::placeholders::_1)),
	_subpath(new ContextPath(std::move(path))),
	_entry(entry->make_copy())
{
}

const dixelu::mctx* ContextPath::SubpathIsSetToValueEntry::__applyImpl(const dixelu::mctx& element) const
{
	bool flag = false;
	const auto result = _subpath->applyAndGetNthEntry(element, flag);
	return result.first;
}

ContextPath::SetOfPathsSetToValues::SetOfPathsSetToValues(SubpathIsSetToValueEntry entry)
{
	_values.emplace_back(std::move(entry));
}

dixelu::mctx& ContextPath::SetOfPathsSetToValues::apply(dixelu::mctx& element) const
{
	auto it = lookupCondition(element);
	return *it;
}

const dixelu::mctx& ContextPath::SetOfPathsSetToValues::apply(const dixelu::mctx& element) const
{
	auto it = lookupCondition(element);
	return *it;
}

dixelu::mctx* ContextPath::SetOfPathsSetToValues::try_apply(dixelu::mctx& element) const
{
	auto result = lookupCondition(element);
	if(result == element.end())
		return nullptr;
	return &*result;
}

const dixelu::mctx* ContextPath::SetOfPathsSetToValues::try_apply(const dixelu::mctx& element) const
{
	auto result = lookupCondition(element);
	if(result == element.end())
		return nullptr;
	return &*result;
}

bool ContextPath::SetOfPathsSetToValues::exists(const dixelu::mctx& element) const
{
	auto& constCastedElement = const_cast<dixelu::mctx&>(element);
	return lookupCondition(constCastedElement) != constCastedElement.end();
}

ContextPath::Entry* ContextPath::SetOfPathsSetToValues::make_copy() const
{
	auto setPtr = new SetOfPathsSetToValues{};
	for(auto& el: _values)
		if(auto ptr = dynamic_cast<SubpathIsSetToValueEntry*>(el.make_copy()))
		{
			setPtr->_values.emplace_back(std::move(*ptr));
			delete ptr;
		}
	return setPtr;
}

std::string ContextPath::SetOfPathsSetToValues::stringify() const
{
	std::string result = "*:{";
	for(auto& el: _values)
		result += "*/" + el.stringifyInternals() + "&";
	result.back() = '}';
	return result;
}

bool ContextPath::SetOfPathsSetToValues::remove(dixelu::mctx& element) const
{
	auto result = lookupCondition(element);
	if(result == element.end())
		return false;
	element.erase(result);
	return true;
}

void ContextPath::SetOfPathsSetToValues::operator|=(SubpathIsSetToValueEntry entry)
{
	_values.emplace_back(std::move(entry));
}

ContextPath::SmallVector<const dixelu::mctx*> ContextPath::SetOfPathsSetToValues::all(const dixelu::mctx& element) const
{
	SmallVector<const dixelu::mctx*> vector;
	auto batchOfIterators = batchLookupCondition(element);
	for(auto& elem: batchOfIterators)
		vector.emplace_back(&(*elem));
	return vector;
}

bool ContextPath::SetOfPathsSetToValues::__all(const dixelu::mctx& ctx) const
{
	return std::ranges::all_of(_values, [&ctx](const auto& el){ return el.f(ctx); });
}

bool ContextPath::SetOfPathsSetToValues::__any(const dixelu::mctx& ctx) const
{
	return std::ranges::any_of(_values, [&ctx](const auto& el){ return el.f(ctx); });
}

dixelu::mctx::value_iter ContextPath::SetOfPathsSetToValues::lookupCondition(dixelu::mctx& source) const
{
	for(auto it = source.begin(); it != source.end(); ++it)
		if(__all(*it))
			return it;
	return source.end();
}

ContextPath::SmallVector<dixelu::mctx::value_iter> ContextPath::SetOfPathsSetToValues::batchLookupCondition(
	dixelu::mctx& source) const
{
	SmallVector<dixelu::mctx::value_iter> allInstances;
	for(auto it = source.begin(); it != source.end(); ++it)
		if(__all(*it))
			allInstances.emplace_back(it);
	return allInstances;
}

auto ContextPath::SetOfPathsSetToValues::lookupCondition(const dixelu::mctx& source) const -> decltype(source.end())
{
	for(auto it = source.begin(); it != source.end(); ++it)
		if(__all(*it))
			return it;
	return source.end();
}

auto ContextPath::SetOfPathsSetToValues::batchLookupCondition(
	const dixelu::mctx& source) const -> SmallVector<decltype(source.end())>
{
	SmallVector<decltype(source.end())> allInstances;
	for(auto it = source.begin(); it != source.end(); ++it)
		if(__all(*it))
			allInstances.emplace_back(it);
	return allInstances;
}

ContextPath::UnidentifiedValue::UnidentifiedValue(UnidentifiedValue&& rhs) noexcept:
	_appliedEntry(std::move(rhs._appliedEntry))
{}

dixelu::mctx& ContextPath::UnidentifiedValue::apply(dixelu::mctx& element) const
{
	if(!_appliedEntry)
		throw std::runtime_error("UnidentifiedValue was not set to use 'apply' function");
	auto& ref = _appliedEntry->apply(element);
	// _appliedEntry.reset();
	return ref;
}

const dixelu::mctx& ContextPath::UnidentifiedValue::apply(const dixelu::mctx& element) const
{
	if(!_appliedEntry)
		throw std::runtime_error("UnidentifiedValue was not set to use 'apply' function");;
	auto& ref = _appliedEntry->apply(element);
	// _appliedEntry.reset();
	return ref;
}

dixelu::mctx* ContextPath::UnidentifiedValue::try_apply(dixelu::mctx& element) const
{
	if(!_appliedEntry)
		return nullptr;
	auto& ref = _appliedEntry->apply(element);
	return &ref;
}

const dixelu::mctx* ContextPath::UnidentifiedValue::try_apply(const dixelu::mctx& element) const
{
	if(!_appliedEntry)
		return nullptr;
	auto& ref = _appliedEntry->apply(element);
	return &ref;
}

bool ContextPath::UnidentifiedValue::exists(const dixelu::mctx& element) const
{
	if(!_appliedEntry)
		throw std::runtime_error("UnidentifiedValue was not set to use 'exists' function");
	return _appliedEntry->exists(element);
}

bool ContextPath::UnidentifiedValue::remove(dixelu::mctx& element) const
{
	if(!_appliedEntry)
		throw std::runtime_error("UnidentifiedValue cannot be used in 'remove' function");
	return _appliedEntry->remove(element);
}

bool ContextPath::UnidentifiedValue::existsAny(const dixelu::mctx& element, const Entry* next, const ContextPath& path,
	size_t nextIndex) const
{
	if(_appliedEntry)
	{
		if(!_appliedEntry->exists(element))
			return false;

		if(next)
			if(path.existsAnyRecursive(_appliedEntry->apply(element), nextIndex))
				return true;
		return false;
	}

	for(const auto& el : element)
	{
		if(next)
		{
			if(path.existsAnyRecursive(el, nextIndex))
				return true;
		}
	}
	return next? false : true;
}

bool ContextPath::UnidentifiedValue::existsAll(const dixelu::mctx& element, const Entry* next, const ContextPath& path,
	size_t nextIndex) const
{

	if(_appliedEntry)
	{
		if(!_appliedEntry->exists(element))
			return false;

		if(next)
			if(!path.existsAllRecursive(_appliedEntry->apply(element), nextIndex))
				return false;
		return true;
	}

	for(const auto& el : element)
	{
		if(next)
		{
			if(!path.existsAllRecursive(el, nextIndex))
				return false;
		}
	}

	return true;
}

bool ContextPath::UnidentifiedValue::removeAny(dixelu::mctx& context, const Entry* next, const ContextPath& path,
	size_t nextIndex) const
{
	if(_appliedEntry)
	{
		if(!next)
			return _appliedEntry->remove(context);
		if(path.removeRecursive(_appliedEntry->apply(context), nextIndex))
			return true;
		return false;
	}
	for(auto& el : context)
	{
		if(next)
		{
			if(path.removeRecursive(el, nextIndex))
				return true;
		}
		else
		{
			if(context.is_array())
			{
				context.erase(context.begin());
				return true;
			}
			else
			{
				context.erase(context.kvbegin());
				return true;
			}
		}
	}
	return false;
}

bool ContextPath::UnidentifiedValue::removeAll(dixelu::mctx& context, const Entry* next, const ContextPath& path,
	size_t nextIndex) const
{
	if(_appliedEntry)
	{
		if(!next)
			return _appliedEntry->remove(context);
		if(path.removeRecursive(_appliedEntry->apply(context), nextIndex))
			return true;
		return false;
	}
	bool allRemoved = true;

	if(context.is_array())
	{
		for(auto it = context.begin();
		    it != context.end(); )
		{
			if(next)
			{
				if(!path.removeRecursive(*it, nextIndex))
					allRemoved = false;
				++it;
			}
			else
			{

				it = context.erase(context.begin());
			}
		}
	}
	else
	{
		for(auto it = context.kvbegin();
		    it != context.kvend(); )
		{
			if(next)
			{
				if(!path.removeRecursive(it->second, nextIndex))
					allRemoved = false;
				++it;
			}
			else
			{

				it = context.erase(context.kvbegin());
			}
		}
	}
	return allRemoved;
}

ContextPath::SmallVector<const dixelu::mctx*> ContextPath::UnidentifiedValue::all(const dixelu::mctx& element) const
{
	if(_appliedEntry)
		return _appliedEntry->all(element);
	SmallVector<const dixelu::mctx*> contents;
	for(auto& el: element)
		contents.push_back(&el);
	return contents;
}

ContextPath::Entry* ContextPath::UnidentifiedValue::make_copy() const
{
	if(_appliedEntry)
		return _appliedEntry->make_copy();
	return new UnidentifiedValue(__UnidentifiedValue{});
}

std::pair<const dixelu::mctx*, int>
ContextPath::applyAndGetNthEntry(
	const dixelu::mctx& ctx,
	bool& putLastContextOnFailure,
	int applyCountReturnElement) const
{
	const dixelu::mctx* rawPtr = &ctx;
	const dixelu::mctx* returnPointer = nullptr;

	bool preLoopState = putLastContextOnFailure;
	int index = 0;
	for(auto& el: _path)
	{
		if(index == applyCountReturnElement)
			returnPointer = rawPtr;

		auto ptr = (el->try_apply(*rawPtr));

		if(!ptr)
		{
			putLastContextOnFailure ^= true; // is treated as error flag;
			break;
		}

		rawPtr = ptr;

		++index;
	}

	if(preLoopState != putLastContextOnFailure)
		returnPointer = rawPtr;

	if(!putLastContextOnFailure && applyCountReturnElement == -1)
		return {rawPtr, index};
	return {returnPointer, index};
}


ContextPath::ContextPath(const ContextPath& copySource)
{
	//printf("OMG A COPY!!@!!@EK!\n");
	for(auto& el: copySource._path)
		_path.emplace_back(el->make_copy());
}

// copy-inducing operators
ContextPath ContextPath::operator/(StringEntry stringEntry) const &
{
	auto copy = *this;
	copy._path.emplace_back(new StringEntry(std::move(stringEntry)));
	return copy;
}

ContextPath ContextPath::operator/(IntegerEntry stringEntry) const &
{
	auto copy = *this;
	copy._path.emplace_back(new IntegerEntry(std::move(stringEntry)));
	return copy;
}

ContextPath ContextPath::operator/(Entry* stringEntry) const &
{
	auto copy = *this;
	copy._path.emplace_back(stringEntry);
	return copy;
}

ContextPath ContextPath::operator/(ContextPath&& subPath) const &
{
	auto copy = *this;
	for(auto& el: subPath._path)
		if(dynamic_cast<PreviousEntry*>(el.get()))
			copy._path.pop_back();
		else
			copy._path.push_back(std::move(el));
	return copy;
}

ContextPath ContextPath::operator/(__PreviousTag) const &
{
	auto copy = *this;
	if(!copy._path.empty())
		copy._path.pop_back();
	else
		copy._path.emplace_back(new PreviousEntry(previous));
	return copy;
}

ContextPath ContextPath::operator/(__UnidentifiedValue) const &
{
	auto copy = *this;
	copy._path.emplace_back(new UnidentifiedValue(variable));
	return copy;
}

ContextPath ContextPath::operator/(__RootTag) const &
{
	auto copy = *this;
	copy._path.emplace_back(new RootEntry($root));
	return copy;
}


// move-inducing operators
ContextPath ContextPath::operator/(StringEntry stringEntry) &&
{
	ContextPath newPath;
	newPath._path = std::move(_path);
	newPath._path.emplace_back(new StringEntry(std::move(stringEntry)));
	return newPath;
}

ContextPath ContextPath::operator/(IntegerEntry stringEntry) &&
{
	ContextPath newPath;
	newPath._path = std::move(_path);
	newPath._path.emplace_back(new IntegerEntry(std::move(stringEntry)));
	return newPath;
}

ContextPath ContextPath::operator/(Entry* stringEntry) &&
{
	ContextPath newPath;
	newPath._path = std::move(_path);
	newPath._path.emplace_back(stringEntry);
	return newPath;
}

ContextPath ContextPath::operator/(ContextPath&& subPath) &&
{
	ContextPath newPath;
	newPath._path = std::move(_path);
	for(auto& el: subPath._path)
		if(dynamic_cast<PreviousEntry*>(el.get()))
			newPath._path.pop_back();
		else
			newPath._path.push_back(std::move(el));
	return newPath;
}

ContextPath ContextPath::operator/(__PreviousTag) &&
{
	ContextPath newPath;
	newPath._path = std::move(_path);
	if(!newPath._path.empty())
		newPath._path.pop_back();
	else
		newPath._path.emplace_back(new PreviousEntry(previous));
	return newPath;
}

ContextPath ContextPath::operator/(__UnidentifiedValue) &&
{
	ContextPath newPath;
	newPath._path = std::move(_path);
	newPath._path.emplace_back(new UnidentifiedValue(variable));
	return newPath;
}

ContextPath ContextPath::operator/(__RootTag) &&
{
	ContextPath newPath;
	newPath._path = std::move(_path);
	newPath._path.emplace_back(new RootEntry($root));
	return newPath;
}

ContextPath& ContextPath::operator=(const ContextPath& rhs)
{
	ContextPath newPath{rhs};
	_path = std::move(newPath._path);
	return *this;
}

dixelu::mctx& ContextPath::operator[](dixelu::mctx& target) const
{
	dixelu::mctx* rawPtr = &target;
	for(auto& el: _path)
		rawPtr = &(el->apply(*rawPtr));
	return *rawPtr;
}

const dixelu::mctx& ContextPath::operator[](const dixelu::mctx& target) const
{
	const dixelu::mctx* rawPtr = &target;
	for(auto& el: _path)
		rawPtr = &(el->apply(*rawPtr));
	return *rawPtr;
}

size_t ContextPath::size() const
{
	return _path.size();
}

bool ContextPath::exists(const dixelu::mctx& target) const
{
	const dixelu::mctx* rawPtr = &target;
	for(auto& el: _path)
		if(!el->exists(*rawPtr))
			return false;
		else
			rawPtr = &(el->apply(*rawPtr));
	return true;
}

bool ContextPath::existsAny(const dixelu::mctx& target) const
{
	return existsAnyRecursive(target, 0);
}

void ContextPath::__setRootContext(const std::shared_ptr<dixelu::mctx>& element) const
{
	std::weak_ptr<dixelu::mctx> weakPtr = element;
	auto f = [weakPtr]() { return weakPtr.lock().get(); };
	for(auto& el: _path)
		if(auto entry = dynamic_cast<RootEntry*>(el.get()))
			entry->applyRootGetter(f);
}

bool ContextPath::existsAnyRecursive(const dixelu::mctx& target, size_t index) const
{
	if (index >= _path.size())
		return true;
	const dixelu::mctx* rawPtr = &target;
	auto& el = _path[index];
	if(auto unidentified = dynamic_cast<UnidentifiedValue*>(el.get()))
	{
		Entry* next = (index + 1 < _path.size()) ? _path[index + 1].get() : nullptr;
		return unidentified->existsAny(*rawPtr, next, *this, index + 1);
	}

	if(!el->exists(*rawPtr))
		return false;
	rawPtr = &(el->apply(*rawPtr));
	return existsAnyRecursive(*rawPtr, index + 1);
}

bool ContextPath::existsAll(const dixelu::mctx& target) const
{
	return existsAllRecursive(target, 0);
}

bool ContextPath::existsAllRecursive(const dixelu::mctx& target, size_t index) const
{
	if (index >= _path.size())
		return true;

	const dixelu::mctx* rawPtr = &target;
	auto& el = _path[index];
	if(auto unidentified = dynamic_cast<UnidentifiedValue*>(el.get()))
	{
		Entry* next = (index + 1 < _path.size()) ? _path[index + 1].get() : nullptr;
		return unidentified->existsAll(*rawPtr, next, *this, index + 1);
	}

	if(!el->exists(*rawPtr))
		return false;
	rawPtr = &(el->apply(*rawPtr));
	return existsAllRecursive(*rawPtr, index + 1);
}

std::string ContextPath::stringify() const
{
	std::string result;
	for (const auto& entry : _path)
	{
		result += entry->stringify();
		result += "/";
	}
	if(!result.empty())
		result.pop_back();
	return result;
}

bool ContextPath::remove(dixelu::mctx& target) const
{
	return removeRecursive(target, 0);
}

ContextPath::SubpathIsSetToValueEntry ContextPath::makeSinglePathSetToValue(ContextPath&& path, IntegerEntry entry)
{
	return SubpathIsSetToValueEntry(std::move(path), std::move(entry));
}

ContextPath::SubpathIsSetToValueEntry ContextPath::makeSinglePathSetToValue(ContextPath&& path, StringEntry entry)
{
	return SubpathIsSetToValueEntry(std::move(path), std::move(entry));
}

ContextPath::SubpathIsSetToValueEntry ContextPath::makeSinglePathSetToValue(ContextPath&& path, UnidentifiedValue entry)
{
	return SubpathIsSetToValueEntry(std::move(path), std::move(entry));
}

const dixelu::mctx* ContextPath::getConditional(const dixelu::mctx& element) const
{
	bool flag = false;
	const auto result = applyAndGetNthEntry(element, flag);
	return result.first;
}

bool ContextPath::removeRecursive(dixelu::mctx& context, size_t index) const
{
	if(index >= _path.size())
		return false;

	auto& el = _path[index];
	if(index == _path.size() - 1)
	{
		if (auto unidentified = dynamic_cast<UnidentifiedValue*>(el.get()))
			return unidentified->removeAll(context, nullptr, *this, index + 1);
		if(!el->remove(context))
			return false;
	}
	else
	{
		if(auto unidentified = dynamic_cast<UnidentifiedValue*>(el.get()))
		{
			if(!unidentified->removeAll(context, _path[index + 1].get(), *this, index + 1))
				return false;
		}
		else
		{
			dixelu::mctx& nextContext = el->apply(context);
			if(!removeRecursive(nextContext, index + 1))
				return false;
		}
	}
	if(context.empty() && index > 0)
	{
		auto& prevEl = _path[index - 1];
		if(!prevEl->remove(context))
			return false;
	}
	return true;
}

template<>
void ContextPath::setSingleVariable<ContextPath::StringEntry>(
	UnidentifiedValue* targetValue,
	StringEntry entryValue) { targetValue->set(entryValue); }

template<>
void ContextPath::setSingleVariable<ContextPath::IntegerEntry>(
	UnidentifiedValue* targetValue,
	IntegerEntry entryValue) { targetValue->set(entryValue); }

ContextPath ContextPathSerializer::deserialize(const std::string& string)
{
	auto tokens = tokenize(string);
	return buildPath(tokens.begin(), tokens.end());
}

std::deque<ContextPathSerializer::Token> ContextPathSerializer::tokenize(const std::string& string)
{
	constexpr const char* regExpression = R"(#|\/|:|\*|{|\}|=|&|-?\d+|".*?"|[\w\s]+)";

	std::regex reg(regExpression);
	std::deque<Token> tokens;

	std::sregex_iterator it(string.cbegin(), string.cend(), reg);
	std::sregex_iterator end;

	for(; it != end; ++it)
	{
		Token token;
		auto tokenValue = it->str();
		if(tokenValue == "#") token.type = TokenType::ROOT;
		else if(tokenValue == "/") token.type = TokenType::DELIM;
		else if(tokenValue == "*") token.type = TokenType::ANY;
		else if(tokenValue == ":") token.type = TokenType::WHICH;
		else if(tokenValue == "{") token.type = TokenType::NEST_START;
		else if(tokenValue == "}") token.type = TokenType::NEST_END;
		else if(tokenValue == "=") token.type = TokenType::EQUALS;
		else if(tokenValue == "&") token.type = TokenType::AND;
		else if(tokenValue.front() == '"')
		{
			token.type = TokenType::STRING;
			token.value = tokenValue.substr(1, tokenValue.size() - 2);
		}
		else if(std::isdigit(tokenValue.back()) && (tokenValue.front() == '-' || std::isdigit(tokenValue.front())))
		{
			token.type = TokenType::NUMBER;
			token.value = std::move(tokenValue);
		}
		else
		{
			token.type = TokenType::STRING;
			token.value = std::move(tokenValue);
		}

		tokens.emplace_back(std::move(token));
	}

	return tokens;
}

ContextPath ContextPathSerializer::buildPath(
	std::deque<Token>::iterator tokensBegin,
	std::deque<Token>::iterator tokensEnd,
	bool forcePathExtensions,
	int nestness)
{
	if(nestness > 5)
		throw std::runtime_error("ContextPath::buildPath: Complex path queries are not supported.");

	ContextPath::SetOfQueriesEntry setOfQueriesEntry;
	ContextPath path;

	for(auto it = tokensBegin; it < tokensEnd; ++it)
	{
		if(it->type == TokenType::ROOT)
		{
			++it;
			if(it->type == TokenType::DELIM)
				path = std::move(path) / $root;
			else if(it != tokensEnd)
				throw std::runtime_error("ContextPath::buildPath: unexpected root");
			else
			{
				path = std::move(path) / ContextPath($root);
				break;
			}
		}
		else if(it->type == TokenType::DELIM)
		{
			// if(setOfQueriesEntry.size())
			// 	path = std::move(path) / setOfQueriesEntry;
		}
		else if(it->type == TokenType::ANY)
		{
			++it;
			if(it == tokensEnd)
			{
				path = std::move(path) / ContextPath::variable;
				break;
			}
			else if(it->type == TokenType::WHICH && it[1].type == TokenType::NEST_START)
			{
				size_t nestingLevel = 1;
				it += 2;
				auto localBegin = it;
				for(; it < tokensEnd && nestingLevel; ++it)
				{
					if(it->type == TokenType::NEST_START)
						nestingLevel++;
					else if(it->type == TokenType::NEST_END)
						nestingLevel--;
				}
				auto localEnd = it;
				auto innerPath = buildPath(localBegin, localEnd, true, nestness + 1);
				path = std::move(path) / std::move(innerPath);
				--it; // current iterator already points past the parseable area -> move back.
			}
			else if(it->type == TokenType::DELIM)
			{
				// if path building just began -> skip (it is used just for more verbose separation?
				if(!path.size() && forcePathExtensions)
					continue;
				path = std::move(path) / ContextPath::variable;
			}
			else
				throw std::runtime_error("ContextPath::buildPath: Expected DELIM or WHICH specifier after ANY");
		}
		else if(it->type == TokenType::WHICH)
			static_cast<void>(0);
		else if(it->type == TokenType::NEST_START)
			static_cast<void>(0);
		else if(it->type == TokenType::NEST_END)
			static_cast<void>(0);
		else if(it->type == TokenType::EQUALS)
		{
			++it;
			if(it == tokensEnd || it->type == TokenType::ANY)
				setOfQueriesEntry |=
					ContextPath::makeSinglePathSetToValue(std::move(path), ContextPath::variable);
			else if(it->type == TokenType::STRING)
				setOfQueriesEntry |=
					ContextPath::makeSinglePathSetToValue(std::move(path), it->value);
			else if(it->type == TokenType::NUMBER)
				setOfQueriesEntry |=
					ContextPath::makeSinglePathSetToValue(std::move(path), std::stoll(it->value));
			path = {};
		}
		else if(it->type == TokenType::AND)
			static_cast<void>(0);
		else if(it->type == TokenType::STRING)
			path = std::move(path) / it->value;
		else if(it->type == TokenType::NUMBER)
			path = std::move(path) / std::stoll(it->value);
	}

	if(setOfQueriesEntry.size())
		path = std::move(path) / setOfQueriesEntry;

	return path;
}

LinkedContextWrapper::LinkedContextWrapper(std::shared_ptr<dixelu::mctx>&& rootContext):
	_rootContext(rootContext),
	_currentNode(_rootContext.get())
{}

LinkedContextWrapper LinkedContextWrapper::deepClone() const
{
	LinkedContextWrapper newWrp(std::make_shared<dixelu::mctx>(*_rootContext));
	return newWrp;
}

LinkedContextWrapper LinkedContextWrapper::operator[](std::string key) const
{
	return __getSubfield(std::move(key));
}

LinkedContextWrapper LinkedContextWrapper::operator[](ContextPath path) const
{
	path.__setRootContext(_rootContext);
	auto links = __getByPath(path);
	if(!links.empty())
		return links.front();
	return makeShallowCopy();
}

void LinkedContextWrapper::applyRenderedContextDiff__internal(
	const dixelu::mctx& diff,
	bool& dropCurrentField,
	bool forceNoDiffForwarding) const
{
	if(!_currentNode)
		return;

	const auto recastedNodePtr = const_cast<dixelu::mctx*>(_currentNode);

	bool doesNotExist = false;
	auto linkedContexts = getAllLinkedContexts(_currentNode, doesNotExist);
	if(doesNotExist)
	{
		if(diff.empty())
			return;
		if(diff.is_scalar())
		{
			*recastedNodePtr = diff;
		}
		else if(diff.is_array())
		{
			while(recastedNodePtr->size() < diff.size())
				recastedNodePtr->push_back(dixelu::mctx());

			auto diffArrayBegin = diff.begin();
			auto currentNodeBegin = recastedNodePtr->begin();
			auto currentWrapperIt = begin();
			const auto currentWrapperEnd = end();
			for(;diffArrayBegin != diff.end();
			     ++diffArrayBegin, ++currentNodeBegin, ++currentWrapperIt)
			{
				bool elementDrop = false;
				auto currentWrapperUnwrapped = *currentWrapperIt;
				if(currentWrapperIt != currentWrapperEnd)
					currentWrapperUnwrapped.applyRenderedContextDiff__internal(
						*diffArrayBegin,
						elementDrop,
						forceNoDiffForwarding);
				else
					elementDrop = true;

				if(elementDrop)
				{
					recastedNodePtr->erase(currentNodeBegin, recastedNodePtr->end());
					break;
				}
			}
		}
		else if(diff.is_object())
		{
			auto additionIt = diff.find("+");
			auto deletionIt = diff.find("-");

			const auto currentWrapperEnd = end();

			if(additionIt == diff.end() && deletionIt == diff.end())
			{
				for(auto diffIt = diff.kvbegin();
				    diffIt != diff.kvend();
				    ++diffIt)
				{
					auto& key = diffIt->first;
					auto& value = diffIt->second;

					bool elementDrop = false;
					auto thisKeyElementIt = find(key);
					if(thisKeyElementIt == currentWrapperEnd)
					{
						/*if(!recastedNodePtr->is_object())
							*recastedNodePtr = dixelu::mctx::make_object();*/
						(*recastedNodePtr)[key]; // double search for insertion and getting iter, bruh
						thisKeyElementIt = find(key);
					}

					auto unwrappedWrapper = *thisKeyElementIt;
					unwrappedWrapper.applyRenderedContextDiff__internal(
						value,
						elementDrop,
						forceNoDiffForwarding);
					if(elementDrop)
						recastedNodePtr->erase(key);
				}
			}
			else if(additionIt == diff.end())
			{
				dropCurrentField = true;
			}
			else
				*recastedNodePtr = *additionIt;
		}
	}
	else if(!forceNoDiffForwarding)
	{
		auto is_array = _currentNode->get_as<bool>(ARRAY_TAG, false);

		auto thisContextDynamicWrapper = makeShallowCopy();

		if(!is_array)
		{
			if(linkedContexts.empty())
				return;

			bool elementDrop = false;
			const auto& firstLinkedContext = linkedContexts.front();
			thisContextDynamicWrapper._currentNode = firstLinkedContext;
			thisContextDynamicWrapper.applyRenderedContextDiff__internal(
				diff,
				elementDrop,
				forceNoDiffForwarding);

			if(elementDrop)
			{
				const auto recastedLinkedContext = const_cast<dixelu::mctx*>(firstLinkedContext);
				recastedLinkedContext->clear();
			}
		}
		else
		{
			auto singleLinkedListIt = linkedContexts.begin();
			auto singleDiffElementIt = diff.begin();
			for(; singleLinkedListIt != linkedContexts.end() &&
				singleDiffElementIt != diff.end();
				++singleLinkedListIt, ++singleDiffElementIt)
			{
				auto& singleLinkedContext = *singleLinkedListIt;
				auto& singleDiffElement = *singleDiffElementIt;
				thisContextDynamicWrapper._currentNode = singleLinkedContext;
				bool elementDrop = false;
				thisContextDynamicWrapper.applyRenderedContextDiff__internal(
					singleDiffElement,
					elementDrop,
					forceNoDiffForwarding);
				if(elementDrop)
				{
					const auto recastedLinkedContext = const_cast<dixelu::mctx*>(singleLinkedContext);
					recastedLinkedContext->clear();
				}
			}
		}
	}
}

void LinkedContextWrapper::cloneAndCheckForLinks__recursive(
	dixelu::mctx& renderedContext,
	size_t linkageRecursivenessDepth,
	bool clone) const
{
	constexpr size_t maxDepth = 10;
	struct TooDeepLinkLoop {};
	if(linkageRecursivenessDepth > maxDepth)
		return void(renderedContext = TooDeepLinkLoop{});

	if(!_currentNode)
		return void(renderedContext = nullptr);

	if(_currentNode->is_scalar() && !renderedContext.is_scalar())
		return void(renderedContext = *_currentNode);

	if(clone)
		renderedContext = *_currentNode;

	auto renderedCtxIt = renderedContext.begin();
	auto thisWrapperIt = begin();
	for(;
		thisWrapperIt != end() && renderedCtxIt != renderedContext.end();
		++thisWrapperIt, ++renderedCtxIt)
	{
		bool doesNotExist = false;
		auto currentContextWrapper = *thisWrapperIt;
		auto currentNode = currentContextWrapper._currentNode;
		auto linkedContexts =
			getAllLinkedContexts(currentNode, doesNotExist);

		auto& thisRenderedContext = *renderedCtxIt;
		if(doesNotExist)
		{
			currentContextWrapper.cloneAndCheckForLinks__recursive(
				thisRenderedContext,
				linkageRecursivenessDepth,
				false);
			continue;
		}

		bool shouldBeTreatedAsArrayOfReferences =
			currentNode->get<bool>(ARRAY_TAG, false);
		thisRenderedContext = nullptr;
		LinkedContextWrapper reusedContextWrapper = makeShallowCopy();

		if(!shouldBeTreatedAsArrayOfReferences && !linkedContexts.empty())
		{
			size_t depth = linkageRecursivenessDepth;
			reusedContextWrapper._currentNode = linkedContexts.front();
			while(!linkedContexts.empty() && depth < maxDepth)
			{
				auto currentLinkedContextNode = linkedContexts.front();
				linkedContexts =
					getAllLinkedContexts(currentLinkedContextNode, doesNotExist);
				if(!linkedContexts.empty())
					reusedContextWrapper._currentNode = linkedContexts.front();
				++depth;
			}

			reusedContextWrapper.cloneAndCheckForLinks__recursive(
				thisRenderedContext,
				depth,
				true);
		}
		else if(shouldBeTreatedAsArrayOfReferences)
		{
			auto& arrayOfReferences = thisRenderedContext;
			arrayOfReferences = dixelu::mctx::make_array();

			struct LinkedReferences
			{
				const dixelu::mctx* node;
				size_t depth;
			};

			std::stack<LinkedReferences> stack;
			for(auto& el: linkedContexts)
				stack.push({el, linkageRecursivenessDepth});

			while(!stack.empty())
			{
				auto currentNodeInfo = stack.top();
				stack.pop();

				auto linkedSubContexts =
					getAllLinkedContexts(currentNodeInfo.node, doesNotExist);
				if(currentNodeInfo.depth < maxDepth && !linkedSubContexts.empty())
					for(auto& singleLinkedContext : linkedSubContexts)
						stack.push({singleLinkedContext, linkageRecursivenessDepth + 1});

				else
				{
					reusedContextWrapper._currentNode = currentNodeInfo.node;
					dixelu::mctx renderedSubContext;
					reusedContextWrapper.cloneAndCheckForLinks__recursive(
						renderedSubContext,
						currentNodeInfo.depth + 1,
						true);
					arrayOfReferences.push_back(std::move(renderedSubContext));
				}
			}
		}
	}
}

void LinkedContextWrapper::deserializeContainedPaths(dixelu::mctx& ctx)
{
	if(ctx.is_object())
	{
		auto it = ctx.find(REF_TAG);
		if(it != ctx.end() && it->is<std::string>())
			*it = ContextPathSerializer::deserialize(it->as<std::string>());

		for(auto& el: ctx)
			deserializeContainedPaths(el);
	}
	else if(ctx.is_array())
	{
		for(auto& el: ctx)
			deserializeContainedPaths(el);
	}
}

LinkedContextWrapper LinkedContextWrapper::makeShallowCopy() const
{
	LinkedContextWrapper wrp;
	wrp._rootContext = _rootContext;
	return wrp;
}

ContextPath::SmallVector<LinkedContextWrapper> LinkedContextWrapper::__getByPath(ContextPath& path) const
{
	LinkedContextWrapper wrapper;
	wrapper._rootContext = _rootContext;
	if(_currentNode == nullptr)
		return {wrapper};

	path.__setRootContext(_rootContext);
	ContextPath::SmallVector<LinkedContextWrapper> wrappers;
	auto result =
		path.getAllMatching<ContextPath::SmallVector>(_currentNode);
	for(auto& matchingNode: result)
	{
		wrapper._currentNode = matchingNode;
		wrappers.emplace_back(wrapper);
	}
	return wrappers;
}

LinkedContextWrapper LinkedContextWrapper::__getSubfield(std::string&& key) const
{
	if (_currentNode == nullptr)
		return *this;

	LinkedContextWrapper newWrapper;
	newWrapper._rootContext = _rootContext;

	auto fieldObjectIt = _currentNode->find(key);
	if(fieldObjectIt == _currentNode->end())
		return newWrapper;

	const auto& contextRef = *fieldObjectIt;
	bool doesNotExist = false;
	auto linkedContext = getLinkedContext(&contextRef, doesNotExist);

	if(doesNotExist || !linkedContext)
		newWrapper._currentNode = &contextRef;
	else
		newWrapper._currentNode = linkedContext;

	return newWrapper;
}

const dixelu::mctx* LinkedContextWrapper::getLinkedContext(const dixelu::mctx* nodeContext, bool& doesNotExist) const
{
	auto referenceTag = nodeContext->find(REF_TAG);
	if (!nodeContext->is_object() || referenceTag == nodeContext->end())
		return doesNotExist = true, nullptr;

	ContextPath path;
	if (referenceTag->is<std::string>())
		path = ContextPathSerializer::deserialize(referenceTag->as<std::string>());
	else if(!referenceTag->is<ContextPath>())
	{
		doesNotExist = false;
		return nullptr;
	}
	else
		path = referenceTag->as<ContextPath>();

	path.__setRootContext(_rootContext);
	auto res = path.getConditional(*nodeContext);
	doesNotExist = res == nullptr;
	return res;
}

ContextPath::SmallVector<const dixelu::mctx*> LinkedContextWrapper::getAllLinkedContexts(
	const dixelu::mctx* nodeContext,
	bool& doesNotExist) const
{
	auto referenceTag = nodeContext->find(REF_TAG);
	if (!nodeContext->is_object() || referenceTag == nodeContext->end())
	{
		doesNotExist = true;
		return {};
	}

	ContextPath path;
	if (referenceTag->is<std::string>())
		path = ContextPathSerializer::deserialize(referenceTag->as<std::string>());
	else if(!referenceTag->is<ContextPath>())
	{
		doesNotExist = false;
		return {};
	}
	else
		path = referenceTag->as<ContextPath>();

	path.__setRootContext(_rootContext);
	return path.getAllMatching<ContextPath::SmallVector>(*nodeContext);
}


LinkedContextWrapper::TrivialPseudoIterator LinkedContextWrapper::begin() const
{
	return TrivialPseudoIterator(_rootContext, _currentNode->begin());
}

LinkedContextWrapper::TrivialPseudoIterator LinkedContextWrapper::end() const
{
	return TrivialPseudoIterator(_rootContext, _currentNode->end());
}

LinkedContextWrapper::TrivialPseudoIterator LinkedContextWrapper::find(const std::string& key) const
{
	if(!_currentNode)
		return TrivialPseudoIterator(_rootContext, dixelu::mctx::value_iter{});
	return TrivialPseudoIterator(_rootContext, _currentNode->find(key));
}

dixelu::mctx LinkedContextWrapper::renderAsLinkedContext() const
{
	dixelu::mctx result;
	cloneAndCheckForLinks__recursive(result, 0, true);
	return result;
}

void LinkedContextWrapper::applyRenderedContextDiff(const dixelu::mctx& diff, bool forceNoDiffForwarding) const
{
	bool dropField = false;
	applyRenderedContextDiff__internal(diff, dropField, forceNoDiffForwarding);
}

LinkedContextWrapper::TrivialPseudoIterator::TrivialPseudoIterator(
	std::shared_ptr<dixelu::mctx> rootContext,
        IteratorKind&& iter):
	_it(iter),
	_root(std::move(rootContext))
{
}

LinkedContextWrapper::TrivialPseudoIterator LinkedContextWrapper::TrivialPseudoIterator::operator++()
{
	++_it;
	return *this;
}

LinkedContextWrapper::TrivialPseudoIterator LinkedContextWrapper::TrivialPseudoIterator::operator++(int)
{
	TrivialPseudoIterator copy = *this;
	++_it;
	return copy;
}

LinkedContextWrapper::TrivialPseudoIterator LinkedContextWrapper::TrivialPseudoIterator::operator--()
{
	--_it;
	return *this;
}

LinkedContextWrapper::TrivialPseudoIterator LinkedContextWrapper::TrivialPseudoIterator::operator--(int)
{
	TrivialPseudoIterator copy = *this;
	--_it;
	return copy;
}

bool LinkedContextWrapper::TrivialPseudoIterator::operator==(const TrivialPseudoIterator& other) const
{
	return _it == other._it;
}

bool LinkedContextWrapper::TrivialPseudoIterator::operator!=(const TrivialPseudoIterator& other) const
{
	return _it != other._it;
}

LinkedContextWrapper LinkedContextWrapper::TrivialPseudoIterator::operator*() const
{
	LinkedContextWrapper wrp{static_cast<decltype(_rootContext)>(_root)};
	wrp._currentNode = &(*_it);
	return wrp;
}

LinkedContextWrapper LinkedContextWrapper::TrivialPseudoIterator::operator->() const
{
	LinkedContextWrapper wrp{static_cast<decltype(_rootContext)>(_root)};
	wrp._currentNode = &(*_it);
	return wrp;
}


inline namespace literals
{

ContextPath operator""_ctxpath(const char* str, size_t)
{
	return ContextPathSerializer::deserialize(str);
}

}

int static_construction_test()
{
	std::string path1 =
		R"(#/data/*:{*/aasd/id=24&*/bxcv/qwer="ajsdhksjd654-asdflhfkjsdfh\"&"as"=null}/x29/asdjfkj)";
		// "#/asddsa/*:{*/asddsa=*}/use_cuda";

	std::cout << "Before (once): " << path1 << std::endl;
	auto path = ContextPathSerializer::deserialize(path1);
	auto stringified = ContextPathSerializer::serialize(path);

	std::cout << "After (once): " << stringified << std::endl;
	auto path2 = ContextPathSerializer::deserialize(path1);
	auto stringified2 = ContextPathSerializer::serialize(path2);

	std::cout << "After (2ice): " << stringified2 << std::endl;
	return 23904;
}

int static_construction_test_2()
{
	std::string path1 =
		R"(#/data/*:{*/x="1"}/test)";

	dixelu::mctx ctxR;
	auto& data = ctxR["data"];
	data["a"]["x"] = 4;
	data["a"]["test"] = "nope";
	data["b"]["s"] = "no field x to test";
	data["c"]["x"] = "f";
	data["c"]["test"] = "not a correct string";
	data["d"]["x"] = "1";
	data["q"] = nullptr;
	data["d"]["test"] = "YEAS!!!";
	data["e"]["x"] = "1";
	data["e"]["test"]["abcd"] = "WELL ...";
	data["e"]["test"]["just_different_string"] = true;
	data["r"] = {1, 2, 4, 8}; // todo: constructor from initialiser list
	data["obj"]["$ref"] = path1;
	data["obj"]["$as_array"] = true;

	std::shared_ptr<dixelu::mctx> ptr =
		std::make_shared<dixelu::mctx>(std::move(ctxR));
	auto& ctx = *ptr;

	LinkedContextWrapper wrp(std::move(ptr));
	auto wrpData = wrp["data"];
	auto wrpR = wrpData["r"];
	auto& node = wrpR.getUnderlyingContextNode();

	std::cout << node.is_object() << std::endl;

	auto beforeDeserialize = std::chrono::steady_clock::now();
	auto path = ContextPathSerializer::deserialize(path1);
	auto beforeAccess = std::chrono::steady_clock::now();

	auto& ref = path[ctx];
	auto afterAccess = std::chrono::steady_clock::now();

	auto& resultingString = ref.as<std::string>();
	std::cout << resultingString << std::endl;

	std::cout << "Deserialization (mksec): " <<
		std::chrono::duration_cast<std::chrono::microseconds>(beforeAccess - beforeDeserialize).count()
	<< std::endl;

	std::cout << "Access (mksec): " <<
		std::chrono::duration_cast<std::chrono::microseconds>(afterAccess - beforeAccess).count()
	<< std::endl;

	auto renderedCtx =
		wrp.renderAsLinkedContext();

	std::cout << "Rendered context: " <<
		dixelu::mctx_json::serialize(renderedCtx)
	<< std::endl;

	auto newVersion = renderedCtx;
	newVersion["data"]["obj"][0]["z"]["yield"] = "something";
	newVersion["data"]["a"]["add"] = "stuff";
	newVersion["data"]["e"]["test"]["yield_different"] = 3490;
	newVersion["data"]["e"]["test"].erase("just_different_string");

	dixelu::mctx xczxc;
	xczxc["asdf"] = 934;
	data["r"].push_back(xczxc);
	auto diff = getDifference(renderedCtx, newVersion);

	std::cout << "Diff: " <<
		dixelu::mctx_json::serialize(diff)
	<< std::endl;

	auto clonedSettings = wrp.deepClone();
	wrp.clear();

	clonedSettings.applyRenderedContextDiff(diff);
	auto updatedRenderedSettings =
		clonedSettings.renderAsLinkedContext();

	std::cout << "Updated context: " <<
		dixelu::mctx_json::serialize(updatedRenderedSettings)
	<< std::endl;

	return 3213;
}

//int ____xw3984fu93w4if = static_construction_test_2();