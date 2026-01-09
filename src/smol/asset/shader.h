#pragma once

#include "smol/asset.h"
#include "smol/color.h"
#include "smol/defines.h"
#include "smol/math_util.h"
#include "smol/rendering/renderer.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

#include <vulkan/vulkan_core.h>

namespace smol
{
    struct shader_uniform_member_t
    {
        std::string name;
        u32 offset = 0;
        u32 size = 0;
        u32 set = 0;
        u32 binding = 0;
    };

    struct descriptor_binding_t
    {
        u32 set = 0;
        u32 binding = 0;
        u32 count = 1;
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    };

    struct shader_reflection_t
    {
        std::unordered_map<std::string, descriptor_binding_t> bindings;
        std::unordered_map<std::string, shader_uniform_member_t> uniform_members;

        u32 get_ubo_size(u32 set, u32 binding) const;
        std::vector<VkDescriptorSetLayoutBinding> get_layout_bindings_for_set(u32 set) const;
    };

    struct shader_data_t
    {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

        VkDescriptorSetLayout material_set_layout = VK_NULL_HANDLE;

        shader_reflection_t reflection;

        ~shader_data_t();
    };

    struct shader_asset_t
    {
        std::shared_ptr<shader_data_t> shader_data = std::make_shared<shader_data_t>();

        bool ready() const { return shader_data->pipeline != VK_NULL_HANDLE; }
        shader_data_t* get_data() const { return shader_data.get(); }
    };

    template<>
    struct asset_loader_t<shader_asset_t>
    {
        static std::optional<shader_asset_t> load(const std::string& path);
    };
} // namespace smol
