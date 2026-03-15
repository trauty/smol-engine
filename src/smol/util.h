#pragma once

#include "smol/defines.h"
#include "smol/log.h"
#include "smol/rendering/vulkan.h"

#include <string>
#include <vector>

namespace smol::util
{
    std::string read_file(const std::string& path);
    std::vector<i8> read_file_raw(const std::string& path);

    template <typename T>
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