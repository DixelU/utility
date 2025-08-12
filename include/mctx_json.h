#pragma once

#include "mctx.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace dixelu::mctx_json
{

using json = nlohmann::json;

json serialize_mctx(const mctx& value);
mctx deserialize_mctx(const json& j);

std::string serialize(const mctx& value);
std::string serialize_pretty(const mctx& value);

mctx deserialize(const std::string& json_str);

} // namespace dixelu::mctx_json
