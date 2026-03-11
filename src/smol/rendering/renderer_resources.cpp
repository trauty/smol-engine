#include "renderer_resources.h"

#include "smol/log.h"
#include "smol/rendering/renderer_types.h"
#include "vulkan/vulkan_core.h"

#include <cstring>

/*
namespace smol::renderer
{
    static resource_system_t res_system;

    void create_buffer(render_context_t& ctx, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& out_buf,
                       VmaAllocation& out_alloc, void** out_mapped)
    {
        VkBufferCreateInfo buf_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buf_info.size = size;
        buf_info.usage = usage;

        VmaAllocationCreateInfo alloc_info = {};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VK_CHECK(vmaCreateBuffer(ctx.allocator, &buf_info, &alloc_info, &out_buf, &out_alloc, nullptr));

        if (out_mapped)
        {
            VmaAllocationInfo vma_info;
            vmaGetAllocationInfo(ctx.allocator, out_alloc, &vma_info);
            *out_mapped = vma_info.pMappedData;
        }
    }

    bool init_resources(render_context_t& ctx)
    {
        VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VK_CHECK(vkCreateSampler(ctx.device, &sampler_info, nullptr, &res_system.linear_sampler));

        create_buffer(ctx, sizeof(gpu_object_data_t) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      res_system.object_buffer, res_system.object_alloc, (void**)&res_system.object_mapped);

        create_buffer(ctx, sizeof(gpu_material_data_t) * MAX_MATERIALS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      res_system.material_buffer, res_system.material_alloc, (void**)&res_system.material_mapped);

        create_buffer(ctx, sizeof(gpu_global_data_t), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, res_system.global_buffer,
                      res_system.global_alloc, (void**)&res_system.global_mapped);

        create_buffer(ctx, sizeof(gpu_light_t) * MAX_LIGHTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      res_system.light_buffer, res_system.light_alloc, (void**)&res_system.light_mapped);

        VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
                                             {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
                                             {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_BINDLESS_TEXTURES},
                                             {VK_DESCRIPTOR_TYPE_SAMPLER, 10}};

        VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
        pool_info.maxSets = 4;
        pool_info.poolSizeCount = 4;
        pool_info.pPoolSizes = pool_sizes;
        VK_CHECK(vkCreateDescriptorPool(ctx.device, &pool_info, nullptr, &res_system.descriptor_pool));

        VkDescriptorSetLayoutBinding scene_binds[] = {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
             nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
             nullptr},
            {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_BINDLESS_TEXTURES, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &res_system.linear_sampler}};

        VkDescriptorBindingFlags scene_flags[] = {
            0, 0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT, 0};
        VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
        flags_info.bindingCount = 4;
        flags_info.pBindingFlags = scene_flags;

        VkDescriptorSetLayoutCreateInfo scene_layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        scene_layout_info.pNext = &flags_info;
        scene_layout_info.bindingCount = 4;
        scene_layout_info.pBindings = scene_binds;
        scene_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
        VK_CHECK(vkCreateDescriptorSetLayout(ctx.device, &scene_layout_info, nullptr, &res_system.scene_layout));

        // pass descriptor set
        VkDescriptorSetLayoutBinding pass_binds[] = {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT,
             nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
             nullptr}};

        VkDescriptorSetLayoutCreateInfo pass_layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        pass_layout_info.bindingCount = 4;
        pass_layout_info.pBindings = pass_binds;
        VK_CHECK(vkCreateDescriptorSetLayout(ctx.device, &pass_layout_info, nullptr, &res_system.pass_layout));

        u32_t max_textures = MAX_BINDLESS_TEXTURES;
        VkDescriptorSetVariableDescriptorCountAllocateInfo variable_info = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO};
        variable_info.descriptorSetCount = 1;
        variable_info.pDescriptorCounts = &max_textures;

        VkDescriptorSetAllocateInfo scene_alloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        scene_alloc.pNext = &variable_info;
        scene_alloc.descriptorPool = res_system.descriptor_pool;
        scene_alloc.descriptorSetCount = 1;
        scene_alloc.pSetLayouts = &res_system.scene_layout;
        VK_CHECK(vkAllocateDescriptorSets(ctx.device, &scene_alloc, &res_system.scene_set));

        VkDescriptorSetAllocateInfo pass_alloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        pass_alloc.descriptorPool = res_system.descriptor_pool;
        pass_alloc.descriptorSetCount = 1;
        pass_alloc.pSetLayouts = &res_system.pass_layout;
        VK_CHECK(vkAllocateDescriptorSets(ctx.device, &pass_alloc, &res_system.pass_set));

        VkDescriptorBufferInfo obj_info = {res_system.object_buffer, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo mat_info = {res_system.object_buffer, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo global_info = {res_system.object_buffer, 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo light_info = {res_system.object_buffer, 0, VK_WHOLE_SIZE};

        VkWriteDescriptorSet writes[] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, res_system.scene_set, 0, 0,
                                          1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &obj_info, nullptr},
                                         {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, res_system.scene_set, 1, 0,
                                          1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &mat_info, nullptr},
                                         {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, res_system.pass_set, 0, 0, 1,
                                          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &global_info, nullptr},
                                         {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, res_system.pass_set, 1, 0, 1,
                                          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &light_info, nullptr}};

        vkUpdateDescriptorSets(ctx.device, 4, writes, 0, nullptr);

        res_system.free_texture_indices.reserve(MAX_BINDLESS_TEXTURES);
        for (u32_t i = MAX_BINDLESS_TEXTURES - 1; i > 0; i--) { res_system.free_texture_indices.push_back(i); }

        return true;
    } // namespace smol::renderer

    void shutdown_resources(render_context_t& ctx)
    {
        vkDeviceWaitIdle(ctx.device);

        vkDestroyDescriptorSetLayout(ctx.device, res_system.scene_layout, nullptr);
        vkDestroyDescriptorSetLayout(ctx.device, res_system.pass_layout, nullptr);
        vkDestroyDescriptorPool(ctx.device, res_system.descriptor_pool, nullptr);
        vkDestroySampler(ctx.device, res_system.linear_sampler, nullptr);

        auto safe_destroy = [&](VkBuffer& buf, VmaAllocation& allocation) {
            if (buf)
            {
                vmaDestroyBuffer(ctx.allocator, buf, allocation);
                buf = VK_NULL_HANDLE;
            }
        };

        safe_destroy(res_system.object_buffer, res_system.object_alloc);
        safe_destroy(res_system.material_buffer, res_system.material_alloc);
        safe_destroy(res_system.global_buffer, res_system.global_alloc);
        safe_destroy(res_system.light_buffer, res_system.light_alloc);
    }

    texture_handle_t upload_texture(render_context_t& ctx, void* pixels, u32_t w, u32_t h)
    {
        if (res_system.free_texture_indices.empty())
        {
            SMOL_LOG_ERROR("RENDERER", "Max textures exceeded");
            return {};
        }

        u32_t idx = res_system.free_texture_indices.back();
        res_system.free_texture_indices.pop_back();

        VkImageCreateInfo img_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        img_info.imageType = VK_IMAGE_TYPE_2D;
        img_info.extent = {w, h, 1};
        img_info.mipLevels = 1;
        img_info.arrayLayers = 1;
        img_info.format = VK_FORMAT_R8G8B8A8_SRGB;
        img_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        img_info.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo alloc_info = {};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

        VkImage img;
        VmaAllocation alloc;
        vmaCreateImage(ctx.allocator, &img_info, &alloc_info, &img, &alloc, nullptr);

        VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = img;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;

        VkImageView view;
        vkCreateImageView(ctx.device, &view_info, nullptr, &view);

        VkDescriptorImageInfo desc_img = {};
        desc_img.imageView = view;
        desc_img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        desc_img.sampler = VK_NULL_HANDLE;

        VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = res_system.scene_set;
        write.dstBinding = 2;
        write.dstArrayElement = idx;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.descriptorCount = 1;
        write.pImageInfo = &desc_img;

        vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);

        return {idx};
    }

    void update_scene_data(std::span<const gpu_object_data_t> objects, std::span<const gpu_material_data_t> materials)
    {
        if (!objects.empty()) { std::memcpy(res_system.global_mapped, objects.data(), objects.size_bytes()); }
        if (!materials.empty()) { std::memcpy(res_system.material_mapped, materials.data(), materials.size_bytes()); }
    }

    void update_pass_data(const gpu_global_data_t& global_data, std::span<const gpu_light_t> lights)
    {
        std::memcpy(res_system.global_mapped, &global_data, sizeof(gpu_global_data_t));

        if (!lights.empty()) { std::memcpy(res_system.light_mapped, lights.data(), lights.size_bytes()); }
    }

    void bind_frame_resources(VkCommandBuffer cmd, VkPipelineLayout layout)
    {
        VkDescriptorSet sets[] = {res_system.scene_set, res_system.pass_set};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 2, sets, 0, nullptr);
    }

    const resource_system_t& get_resource_system() { return res_system; }
} // namespace smol::renderer

*/