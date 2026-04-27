#pragma once

#include "smol/defines.h"

#include <cstddef>
#include <string_view>

namespace smol
{
    // http://www.isthe.com/chongo/tech/comp/fnv/index.html
    constexpr u32_t hash_string(std::string_view str)
    {
        u32_t hash = 2166136261u;
        for (char c : str)
        {
            hash ^= static_cast<size_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }

    constexpr u64_t hash_string64(std::string_view str)
    {
        u64_t hash = 14695981039346656037ull;
        for (char c : str)
        {
            hash ^= static_cast<size_t>(c);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    template <typename T>
    constexpr u64_t get_type_id()
    {
#if defined(__GNUC__) || defined(__clang__)
        constexpr const char* sig = __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
        constexpr const char* sig = __FUNC_SIG__;
#else
    #error "Compiler not supported"
#endif

        return hash_string64(sig);
    }

    constexpr u32_t operator""_h(const char* str, size_t len) { return hash_string(std::string_view(str, len)); }
    constexpr u64_t operator""_h64(const char* str, size_t len) { return hash_string64(std::string_view(str, len)); }
} // namespace smol