#pragma once
#include "smol/asset.h"
#include "smol/assets/shader.h"
#include "smol/defines.h"

#include <memory>
#include <optional>
#include <string>
#include <vulkan/vulkan_core.h>

namespace smol
{
    enum class texture_format_e
    {
        SRGB,
        LINEAR
    };

    struct texture_data_t
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;

        ~texture_data_t();
    };

    struct texture_t
    {
        i32 width = 0;
        i32 height = 0;
        texture_format_e type = texture_format_e::SRGB;

        std::shared_ptr<texture_data_t> tex_data = std::make_shared<texture_data_t>();

        bool ready() const { return tex_data->image != VK_NULL_HANDLE; }
    };

    template<>
    struct asset_loader_t<texture_t>
    {
        static std::optional<texture_t> load(const std::string& path, texture_format_e type = texture_format_e::SRGB);
    };
} // namespace smol