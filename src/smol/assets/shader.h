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

    struct SMOL_API shader_module_info_t
    {
        std::string name;
        u32_t size;
        std::unordered_map<std::string, shader_member_t> members;

        std::string target_pass = "MainForwardPass";
        std::string blend_mode = "Opaque";
        bool depth_write = true;
        bool depth_test = true;
    };

    struct SMOL_API shader_t
    {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

        std::vector<shader_module_info_t> modules;

        bool is_compute = false;
        std::vector<VkFormat> target_formats;

        bool ready() const { return pipeline_layout != VK_NULL_HANDLE; }
    };

    template <>
    struct SMOL_API asset_loader_t<shader_t>
    {
        static std::optional<shader_t> load(const std::string& path);
        static void unload(shader_t& shader);
    };
} // namespace smol
