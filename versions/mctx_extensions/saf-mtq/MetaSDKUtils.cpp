#include "MetaSDKUtils.h"

#include <mutex>
#include <iostream>

#include <boost/property_tree/ptree.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <cstring>
#include <memory>
#include <vector>
#include <iostream>
#include <boost/uuid/name_generator.hpp>
#include <boost/uuid/name_generator_sha1.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>

#include "mctx_json.h"

dixelu::mctx makeBLOB(const std::string& data)
{
	auto blob = std::make_shared<std::vector<uint8_t>>(data.size());
	std::memcpy(blob->data(), data.data(), data.size());

	return {std::move(blob)};
}

dixelu::mctx getDifference(const dixelu::mctx& _old, const dixelu::mctx& _new)
{
	dixelu::mctx diff;
	bool noMajorDiff = true;
	if(_old.is_scalar() && _new.is_scalar())
	{
		noMajorDiff = (_old == _new); // _old.(_new);
	}
	else if(_old.is_array() && _new.is_array())
	{
		bool areEqualArrays = true;
		auto sizePair = std::minmax({_old.size(), _new.size()});
		for(size_t index = 0; index < sizePair.first; ++index)
		{
			auto difference = getDifference(_old.at(index), _new.at(index));
			areEqualArrays &= difference.is_none();
			diff.push_back(std::move(difference));
		}
		areEqualArrays &= sizePair.first == sizePair.second;
		if(areEqualArrays)
			diff.clear();
		for(size_t index = sizePair.first; index < _old.size(); ++index)
		{
			dixelu::mctx removed;
			removed["-"] = _old.at(index);
			diff.push_back(std::move(removed));
		}
		for(size_t index = sizePair.first; index < _new.size(); ++index)
		{
			dixelu::mctx added;
			added["+"] = _new.at(index);
			diff.push_back(std::move(added));
		}
	}
	else if(_old.is_object() && _new.is_object())
	{
		auto oldIt = _old.kvbegin(), newIt = _new.kvbegin();
		while(oldIt != _old.kvend() || newIt != _new.kvend())
		{
			if(oldIt != _old.kvend() && newIt != _new.kvend() && oldIt->first == newIt->first)
			{
				auto context = getDifference(oldIt->second, newIt->second);
				if(!context.is_none())
					diff[oldIt->first] = std::move(context); // conditional move is UB?
				++oldIt;
				++newIt;
			}

			while(oldIt != _old.kvend() &&
				  (newIt == _new.kvend() || oldIt->first < newIt->first))
			{
				diff[oldIt->first]["-"] = oldIt->second;
				++oldIt;
			}

			while(newIt != _new.kvend() &&
				  (oldIt == _old.kvend() || oldIt->first > newIt->first))
			{
				diff[newIt->first]["+"] = newIt->second;
				++newIt;
			}
		}
	}
	else
		noMajorDiff = false;

	if(!noMajorDiff)
	{
		diff["+"] = _new;
		diff["-"] = _old;
	}

	return diff;
}

dixelu::mctx mergeSettingsContext(const dixelu::mctx& currentSettings, const dixelu::mctx& defaultSettings)
{
	auto mergedSettings = dixelu::mctx(currentSettings);
	if(!defaultSettings.empty())
	{
		for(auto it = defaultSettings.kvbegin();
			it != defaultSettings.kvend();
			++it)
		{
			const auto& key = it->first;
			const auto& defaultValue = it->second;

			auto currentIt = currentSettings.find(key);

			if(currentIt == currentSettings.end())
			{
				mergedSettings[key] = defaultValue;
			}
			else
			{

				if(defaultValue.is_object() && currentIt->is_object())
				{
					mergedSettings[key] = mergeSettingsContext(*currentIt, defaultValue);
				}
			}
		}
	}
	return mergedSettings;
}

std::string deterministicUUID(const std::string& string)
{
	// Predefined namespace UUID (can be customized)
	static const boost::uuids::uuid namespaceUuid =
		boost::uuids::string_generator()("a37ac10b-58cc-4372-a467-0102b2cfd470");
	// Create a UUID generator that uses SHA-1 (version 5 UUID)
	boost::uuids::name_generator gen(namespaceUuid);
	// Generate the UUID based on the input string
	boost::uuids::uuid generated_uuid = gen(string);
	// Convert the UUID to a string and return it
	return boost::uuids::to_string(generated_uuid);
}

std::string demangle(const char* name)
{

	int status{-1}; // some arbitrary value to eliminate the compiler warning

	std::unique_ptr<char, void(*)(void*)> res {
#ifdef __GNUC__
		abi::__cxa_demangle(name, NULL, NULL, &status),
#else
		nullptr,
#endif
		std::free
	};

	return (status==0) ? res.get() : name ;
}

static void __serialiseTypesHelper(const dixelu::mctx& source, dixelu::mctx& target)
{
	if(source.is_none() || source.is_scalar())
	{
		auto mangledName = "l"; // todo: get type name
		target = demangle(mangledName);
	}
	else if(source.is_array())
	{
		target = dixelu::mctx::make_array();
		for(auto& el: source)
		{
			dixelu::mctx arrayElement;
			__serialiseTypesHelper(el, arrayElement);
			target.push_back(std::move(arrayElement));
		}
	}
	else if(source.is_object())
	{
		target = dixelu::mctx::make_object();
		for(auto it = source.kvbegin(); it != source.kvend(); ++it)
		{
			dixelu::mctx objectElement;
			__serialiseTypesHelper(it->second, objectElement);
			target[it->first] = std::move(objectElement);
		}
	}
	else
		throw std::runtime_error("__serialiseTypesHelper unknown context type?");
}

std::string serialiseContextTypes(const dixelu::mctx& source)
{
	dixelu::mctx types;
	__serialiseTypesHelper(source, types);
	return dixelu::mctx_json::serialize(types);
}
