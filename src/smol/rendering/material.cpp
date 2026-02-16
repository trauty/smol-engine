#include "material.h"

#include "smol/assets/shader.h"
#include "smol/defines.h"
#include "smol/log.h"
#include "smol/rendering/renderer.h"

#include <cstring>
#include <mutex>
#include <vulkan/vulkan_core.h>

namespace smol
{
    material_t::~material_t()
    {
        if (renderer::ctx::device != VK_NULL_HANDLE)
        {
            for (auto& [binding, ubo] : ubos)
            {
                if (ubo.buffer) vkDestroyBuffer(renderer::ctx::device, ubo.buffer, nullptr);
                if (ubo.memory)
                {
                    if (ubo.mapped_data) vkUnmapMemory(renderer::ctx::device, ubo.memory);
                    vkFreeMemory(renderer::ctx::device, ubo.memory, nullptr);
                }
            }
        }
    }

    bool material_t::try_build_resources()
    {
        if (!shader.valid()) { return false; }

        if (descriptor_set != VK_NULL_HANDLE) { return false; }

        for (auto& [name, handle] : texture_bindings)
        {
            if (!handle.valid()) return false;
        }

        shader_t* s = shader.get();
        VkDescriptorSetLayout layout = s->shader_data->material_set_layout;
        if (layout == VK_NULL_HANDLE)
        {
            SMOL_LOG_WARN("MATERIAL", "Shader has no material layout; Skipping material init");
            return false;
        }

        {
            std::scoped_lock lock(renderer::ctx::descriptor_mutex);
            VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            alloc_info.pNext = nullptr;
            alloc_info.descriptorPool = renderer::ctx::descriptor_pool;
            alloc_info.descriptorSetCount = 1;
            alloc_info.pSetLayouts = &layout;

            if (vkAllocateDescriptorSets(renderer::ctx::device, &alloc_info, &descriptor_set) != VK_SUCCESS)
            {
                SMOL_LOG_ERROR("MATERIAL", "Failed to allocate descriptor set for material");
                return false;
            }
        }

        for (const auto& [name, bind] : s->shader_data->reflection.bindings)
        {
            if (bind.set != 1) { continue; }

            if (bind.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            {
                ubo_resource_t ubo = {};
                ubo.size = s->shader_data->reflection.get_ubo_size(1, bind.binding);
                if (ubo.size == 0) { ubo.size = 256; } // fallback, if size not found, this should be handled better

                renderer::create_buffer(ubo.size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        ubo.buffer, ubo.memory);

                vkMapMemory(renderer::ctx::device, ubo.memory, 0, ubo.size, 0, &ubo.mapped_data);
                std::memset(ubo.mapped_data, 0, ubo.size);

                VkDescriptorBufferInfo buffer_info = {};
                buffer_info.buffer = ubo.buffer;
                buffer_info.offset = 0;
                buffer_info.range = ubo.size;

                VkWriteDescriptorSet descriptor_set_write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                descriptor_set_write.pNext = nullptr;
                descriptor_set_write.dstSet = descriptor_set;
                descriptor_set_write.dstBinding = bind.binding;
                descriptor_set_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                descriptor_set_write.descriptorCount = 1;
                descriptor_set_write.pBufferInfo = &buffer_info;

                vkUpdateDescriptorSets(renderer::ctx::device, 1, &descriptor_set_write, 0, nullptr);
                ubos[bind.binding] = ubo;

                SMOL_LOG_INFO("MATERIAL", "Created UBO for '{}' at binding: {} with size: {}", name, bind.binding,
                              ubo.size);
            }
            else if (bind.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || bind.type == VK_DESCRIPTOR_TYPE_SAMPLER ||
                     bind.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            {
                auto it = texture_bindings.find(name);

                if (it == texture_bindings.end())
                {
                    SMOL_LOG_WARN("MATERIAL", "No asset assigned for binding '{}'", name);
                    continue;
                }

                texture_t* tex = it->second.get();

                VkDescriptorImageInfo image_info = {};
                image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                if (bind.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
                {
                    image_info.imageView = tex->tex_data->view;
                    image_info.sampler = VK_NULL_HANDLE;
                }
                else if (bind.type == VK_DESCRIPTOR_TYPE_SAMPLER)
                {
                    image_info.imageView = VK_NULL_HANDLE;
                    image_info.sampler = tex->tex_data->sampler;
                }
                else
                {
                    image_info.imageView = tex->tex_data->view;
                    image_info.sampler = tex->tex_data->sampler;
                }

                VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = descriptor_set;
                write.dstBinding = bind.binding;
                write.descriptorType = bind.type;
                write.descriptorCount = 1;
                write.pImageInfo = &image_info;

                vkUpdateDescriptorSets(renderer::ctx::device, 1, &write, 0, nullptr);
            }
        }

        return true;
    }

    void material_t::set_data(const std::string& block_name, const void* data, size_t size)
    {
        auto it = shader->shader_data->reflection.bindings.find(block_name);
        if (it == shader->shader_data->reflection.bindings.end())
        {
            SMOL_LOG_WARN("MATERIAL", "UBO block '{}' not found in shader", block_name);
            return;
        }

        u32 binding = it->second.binding;

        auto ubo_it = ubos.find(binding);
        if (ubo_it == ubos.end()) { return; }

        ubo_resource_t& ubo = ubo_it->second;

        if (size != ubo.size)
        {
            SMOL_LOG_ERROR("MATERIAL", "Data size mismatch in material; Block is {} bytes, but tried to write {}",
                           ubo.size, size);
            return;
        }

        std::memcpy(ubo.mapped_data, data, size);
    }

    void material_t::set_texture(const std::string& tex_name, asset_t<texture_t> texture)
    {
        texture_bindings[tex_name] = texture;
    }
} // namespace smol