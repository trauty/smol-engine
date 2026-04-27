#pragma once

#include "smol/asset.h"
#include "smol/assets/shader.h"
#include "smol/defines.h"
#include "smol/rendering/renderer_constants.h"
#include "smol/rendering/samplers.h"

#include <unordered_map>

namespace smol
{
    struct SMOL_API bound_resource_t
    {
        VkDescriptorType type;

        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        VkDeviceSize size = VK_WHOLE_SIZE;

        VkImageView view = VK_NULL_HANDLE;
        sampler_type_e sampler;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    struct SMOL_API shader_instance_t
    {
        asset_handle_t shader_handle;

        std::unordered_map<u32_t, bound_resource_t> bound_resources;
        std::unordered_map<u32_t, VkDescriptorSet> sets[renderer::MAX_FRAMES_IN_FLIGHT];

        u32_t dirty_frames = renderer::MAX_FRAMES_IN_FLIGHT;
        u32_t last_synced_frame = renderer::BINDLESS_NULL_HANDLE;
        bool is_initialized = false;

        shader_instance_t() = default;
        shader_instance_t(asset_handle_t target_shader);
        ~shader_instance_t();

        void init(asset_handle_t target_shader);
        void shutdown();
        void sync();

        void set_buffer(u32_t name_hash, VkBuffer buffer, VkDeviceSize = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
        void set_image(u32_t name_hash, VkImageView view, sampler_type_e sampler, VkImageLayout layout);

        void bind(VkCommandBuffer cmd);
    };
} // namespace smol