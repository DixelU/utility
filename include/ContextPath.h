// SPDX-License-Identifier: MIT
// Compatibility umbrella for the archived SAF-MTQ include and global names.
#pragma once

#include "context_path.h"
#include "context_path_serializer.h"
#include "linked_context_wrapper.h"

using ContextPath = dixelu::ContextPath;
using ContextPathSerializer = dixelu::ContextPathSerializer;
using LinkedContextWrapper = dixelu::LinkedContextWrapper;

inline namespace literals
{
using dixelu::literals::operator""_ctxpath;
inline constexpr auto $root = dixelu::ContextPath::$root;
}
