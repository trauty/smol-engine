#pragma once

#include <string>
#include <type_traits>
#include <functional>
#include <concepts>
#include <cstddef>

namespace smol::util
{
    std::string get_file_content(const std::string& path);

    template<typename T>
    concept hashable_t = requires(T t) {
        { std::hash<T>{}(t) } -> std::convertible_to<std::size_t>;
    };
}