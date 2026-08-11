// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Alexander Verevkin
#pragma once

#include "context_path.h"

#include <string>
#include <string_view>

namespace dixelu
{

class ContextPathSerializer
{
public:
	[[nodiscard]] static ContextPath deserialize(std::string_view text);
	[[nodiscard]] static std::string serialize(const ContextPath& source)
	{
		return source.stringify();
	}
};

namespace literals
{

[[nodiscard]] ContextPath operator""_ctxpath(const char* text, std::size_t size);
inline constexpr auto $root = ContextPath::$root;

} // namespace literals

} // namespace dixelu
