#pragma once

#include "smol/rendering/renderer_types.h"
#include "vulkan/vulkan_core.h"
#include <span>
#include <vector>

namespace smol::renderer
{
    struct resource_system_t
    {
        VkDescriptorPool descriptor_pool;

        // scene data
        VkDescriptorSetLayout scene_layout;
        VkDescriptorSet scene_set;

        VkBuffer object_buffer;
        VmaAllocation object_alloc;
        gpu_object_data_t* object_mapped;

        VkBuffer material_buffer;
        VmaAllocation material_alloc;
        gpu_object_data_t* material_mapped;

        VkSampler linear_sampler; // reflection missing, should have global registry of samplers for reuse
        std::vector<u32_t> free_texture_indices;

        VkDescriptorSetLayout pass_layout;
        VkDescriptorSet pass_set;

        VkBuffer global_buffer;
        VmaAllocation global_alloc;
        gpu_global_data_t* global_mapped;

        VkBuffer light_buffer;
        VmaAllocation light_alloc;
        gpu_global_data_t* light_mapped;
    };

    bool init_resources(render_context_t& ctx);
    void shutdown_resources(render_context_t& ctx);

    texture_handle_t upload_texture(render_context_t& ctx, void* pixels, u32_t w, u32_t h);

    void update_scene_data(std::span<const gpu_object_data_t> objects, std::span<const gpu_material_data_t> materials);
    void update_pass_data(const gpu_global_data_t& global_data, std::span<const gpu_light_t> lights);

    void bind_frame_resources(VkCommandBuffer cmd, VkPipelineLayout layout);

    const resource_system_t& get_resource_system();
} // namespace smol::renderer