#pragma once

#include "smol/asset.h"
#include "smol/defines.h"

#include <memory>
#include <optional>
#include <string>
#include <vulkan/vulkan_core.h>

namespace smol
{
    struct vertex_t
    {
        f32 position[3];
        f32 normal[3];
        f32 uv[2];
    };

    struct mesh_data_t
    {
        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VkDeviceMemory vertex_memory = VK_NULL_HANDLE;

        VkBuffer index_buffer = VK_NULL_HANDLE;
        VkDeviceMemory index_memory = VK_NULL_HANDLE;

        ~mesh_data_t();
    };

    struct mesh_asset_t
    {
        std::shared_ptr<mesh_data_t> mesh_data = std::make_shared<mesh_data_t>();

        i32 vertex_count = 0;
        i32 index_count = 0;
        bool uses_indices = false;

        bool ready() const { return mesh_data->vertex_buffer != VK_NULL_HANDLE; }
    };

    template<>
    struct asset_loader_t<mesh_asset_t>
    {
        static std::optional<mesh_asset_t> load(const std::string& path);
    };
} // namespace smol