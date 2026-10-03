#pragma once
#include "tau/asset.h"
#include "tau/assets/shader.h"
#include "tau/defines.h"
#include "tau/rendering/vulkan.h"

#include <memory>
#include <optional>
#include <string>

namespace tau
{
    enum class texture_format_e
    {
        SRGB,
        LINEAR
    };

    struct TAU_ENGINE_API texture_t
    {
        i32 width = 0;
        i32 height = 0;
        texture_format_e type = texture_format_e::SRGB;

        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        u32_t bindless_id = 0xfffffff;
    };

    template <>
    struct TAU_ENGINE_API asset_loader_t<texture_t>
    {
        static std::optional<texture_t> load(const std::string& path, texture_format_e type = texture_format_e::SRGB);
        static void unload(texture_t& tex);
    };
} // namespace tau