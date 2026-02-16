#pragma once

#include "smol/defines.h"

namespace smol::hash
{
    // http://www.isthe.com/chongo/tech/comp/fnv/index.html
    constexpr u32_t hashString(std::string_view str)
    {
        u32_t hash = 2166136261u;
        for (char c : str)
        {
            hash ^= static_cast<size_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }

    constexpr u64_t hashString64(std::string_view str)
    {
        u64_t hash = 14695981039346656037ull;
        for (char c : str)
        {
            hash ^= static_cast<size_t>(c);
            hash *= 1099511628211ull;
        }
        return hash;
    }

} // namespace smol::hash