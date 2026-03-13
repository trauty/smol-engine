#pragma once

#include "smol/asset_registry.h"
#include "smol/defines.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace smol
{
    struct shader_member_t
    {
        std::string name;
        u32_t offset = 0;
        u32_t size = 0;
    };

    struct shader_reflection_t
    {
        std::unordered_map<std::string, shader_member_t> members;
        u32_t material_size;
    };

    struct shader_t
    {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
        shader_reflection_t reflection;

        bool ready() const { return pipeline != VK_NULL_HANDLE; }
    };

    template <>
    struct asset_loader_t<shader_t>
    {
        static std::optional<shader_t> load(const std::string& path);
        static void unload(shader_t& shader);
    };
} // namespace smol
