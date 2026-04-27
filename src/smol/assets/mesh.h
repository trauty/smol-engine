#pragma once

#include "smol/asset_fwd.h"
#include "smol/defines.h"
#include "smol/math.h"
#include "smol/rendering/vulkan.h"

#include <optional>
#include <string>

namespace smol
{
    struct SMOL_API vertex_t
    {
        f32 position[3];
        f32 normal[3];
        f32 uv[2];
    };

    struct SMOL_API mesh_t
    {
        u32_t vertex_count = 0;
        u32_t index_count = 0;

        vec3_t local_center;
        f32 local_radius;

        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VmaAllocation vertex_allocation = VK_NULL_HANDLE;

        VkBuffer index_buffer = VK_NULL_HANDLE;
        VmaAllocation index_allocation = VK_NULL_HANDLE;

        u32_t vertex_bindless_id = 0xffffffff;
        u32_t index_bindless_id = 0xffffffff;
    };

    template <>
    struct SMOL_API asset_loader_t<mesh_t>
    {
        static std::optional<mesh_t> load(const std::string& path);
        static void unload(mesh_t& mesh);
    };
} // namespace smol