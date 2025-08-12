#include <mctx_json.h>

dixelu::mctx_json::json dixelu::mctx_json::serialize_mctx(const mctx& value)
{
	if (value.is_none())
		return nullptr;

	if (value.is<bool>())
		return value.get<bool>();

	if (value.is<uint64_t>())
		return value.get<uint64_t>();

	if (value.is<int64_t>())
		return value.get<int64_t>();

	if (value.is<double>())
		return value.get<double>();

	if (value.is<float>())
		return value.get<float>();

	if (value.is<std::string>())
		return value.get<std::string>();

	if (value.is_array())
	{
		json arr = json::array();
		for (const auto& item : value)
			arr.push_back(serialize_mctx(item));
		return arr;
	}

	if (value.is_object())
	{
		json obj = json::object();
		for (auto it = value.kvbegin(); it != value.kvend(); ++it)
			obj[it->first] = serialize_mctx(it->second);
		return obj;
	}

	// Handle custom types - serialize as string with type info
	if (value.is<details::custom_head>())
	{
		const auto& custom_val = value.as<details::custom_head>();
		return json::object({
			{"__custom_type", custom_val.get_type_name()},
			{"value", "[unserializable]"} // Custom types need special handling
		});
	}

	throw std::runtime_error("Unsupported type for JSON serialization");
}

dixelu::mctx dixelu::mctx_json::deserialize_mctx(const json& j)
{
	if (j.is_null())
		return mctx(nullptr);

	if (j.is_boolean())
		return mctx(j.get<bool>());

	if (j.is_number_unsigned())
		return mctx(j.get<uint64_t>());

	if (j.is_number_integer())
		return mctx(j.get<int64_t>());

	if (j.is_number_float())
	{
		// Prefer double if the number has decimal part
		if (j.get<double>() != static_cast<int64_t>(j.get<double>()))
			return mctx(j.get<double>());

		return mctx(j.get<double>());
	}

	if (j.is_string())
		return mctx{j.get<std::string>()};

	if (j.is_array())
	{
		mctx arr = mctx::make_array();
		for (const auto& item : j)
			arr.push_back(deserialize_mctx(item));

		return arr;
	}

	if (j.is_object())
	{
		// Check for custom type marker
		if (j.contains("__custom_type"))
		{
			return {};
			// Custom types would need special handling here
			// throw std::runtime_error("Custom type deserialization not implemented");
		}

		mctx obj = mctx::make_object();
		for (auto it = j.begin(); it != j.end(); ++it)
			obj[it.key()] = deserialize_mctx(it.value());

		return obj;
	}

	throw std::runtime_error("Unknown JSON type during deserialization");
}

std::string dixelu::mctx_json::serialize(const mctx& value)
{
	return serialize_mctx(value).dump();
}

std::string dixelu::mctx_json::serialize_pretty(const mctx& value)
{
	return serialize_mctx(value).dump(1, '\t', true);
}

dixelu::mctx dixelu::mctx_json::deserialize(const std::string& json_str)
{
	return deserialize_mctx(json::parse(json_str));
}
