#pragma once

#include "smol/asset_registry.h"
#include "smol/defines.h"
#include "smol/rendering/vulkan.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace smol
{
    struct SMOL_API shader_member_t
    {
        std::string name;
        u32_t offset = 0;
        u32_t size = 0;
    };

    struct SMOL_API shader_reflection_t
    {
        std::unordered_map<std::string, shader_member_t> members;
        u32_t material_size;
    };

    struct SMOL_API shader_t
    {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
        shader_reflection_t reflection;

        bool ready() const { return pipeline != VK_NULL_HANDLE; }
    };

    template <>
    struct SMOL_API asset_loader_t<shader_t>
    {
        static std::optional<shader_t> load(const std::string& path);
        static void unload(shader_t& shader);
    };
} // namespace smol
