// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Alexander Verevkin
#pragma once

#include "mctx_path.h"

#include <string>
#include <string_view>

namespace dixelu
{

class mctx_path_serializer
{
public:
	[[nodiscard]] static mctx_path deserialize(std::string_view text);
	[[nodiscard]] static std::string serialize(const mctx_path& source)
	{
		return source.stringify();
	}
};

namespace literals
{

[[nodiscard]] mctx_path operator""_mctx_path(const char* text, std::size_t size);

} // namespace literals

} // namespace dixelu
