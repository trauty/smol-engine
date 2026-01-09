#pragma once

#include "smol/log.h"
#include <concepts>
#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>
#include <vulkan/vulkan_core.h>

namespace smol::util
{
    std::string get_file_content(const std::string& path);

    template<typename T>
    bool load_vulkan_function(VkInstance instance, const char* function_name, T& out_func_ptr)
    {
        auto func_ptr = reinterpret_cast<T>(vkGetInstanceProcAddr(instance, function_name));

        if (func_ptr == nullptr)
        {
            SMOL_LOG_ERROR("VULKAN", "Failed to load extension function: {}", function_name);
            return false;
        }

        out_func_ptr = func_ptr;
        return true;
    }
} // namespace smol::util