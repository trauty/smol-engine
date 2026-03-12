#include "renderer_resources.h"

#include "smol/defines.h"
#include "smol/log.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_types.h"
#include "vulkan/vulkan_core.h"

#include <cstring>
#include <mutex>

namespace smol::renderer
{
    resource_system_t res_system;

    void descriptor_heap_t::init(u32_t max_indices)
    {
        capacity = max_indices;
        next_unused = 0;
        free_indices.reserve(1024);
    }

    u32_t descriptor_heap_t::acquire()
    {
        if (!free_indices.empty())
        {
            u32_t idx = free_indices.back();
            free_indices.pop_back();
            return idx;
        }

        return (next_unused < capacity) ? next_unused++ : NULL_HANDLE;
    }

    void descriptor_heap_t::release(u32_t index) { free_indices.push_back(index); }

    void init_resources()
    {
        VkDescriptorSetLayoutBinding bindings[4] = {};

        bindings[0] = {0, VK_DESCRIPTOR_TYPE_SAMPLER, MAX_SAMPLERS, VK_SHADER_STAGE_ALL, nullptr};
        bindings[1] = {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_SAMPLED_TEXTURES, VK_SHADER_STAGE_ALL, nullptr};
        bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_STORAGE_TEXTURES, VK_SHADER_STAGE_ALL, nullptr};
        bindings[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_SSBOS, VK_SHADER_STAGE_ALL, nullptr};

        VkDescriptorBindingFlags flags[4] = {
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        };

        VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .bindingCount = 4,
            .pBindingFlags = flags,
        };

        VkDescriptorSetLayoutCreateInfo layout_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = &flags_info,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
            .bindingCount = 4,
            .pBindings = bindings,
        };

        VK_CHECK(vkCreateDescriptorSetLayout(ctx.device, &layout_info, nullptr, &res_system.global_layout));

        VkDescriptorPoolSize pool_sizes[4] = {
            {VK_DESCRIPTOR_TYPE_SAMPLER,        MAX_SAMPLERS        },
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  MAX_SAMPLED_TEXTURES},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  MAX_STORAGE_TEXTURES},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_SSBOS           },
        };

        VkDescriptorPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
            .maxSets = 1,
            .poolSizeCount = 4,
            .pPoolSizes = pool_sizes,
        };

        VK_CHECK(vkCreateDescriptorPool(ctx.device, &pool_info, nullptr, &res_system.global_pool));

        VkDescriptorSetAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = res_system.global_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &res_system.global_layout,
        };

        VK_CHECK(vkAllocateDescriptorSets(ctx.device, &alloc_info, &res_system.global_set));

        res_system.texture_heap.init(MAX_SAMPLED_TEXTURES);
        res_system.storage_image_heap.init(MAX_STORAGE_TEXTURES);
        res_system.buffer_heap.init(MAX_SSBOS);

        VkSemaphoreTypeCreateInfo sem_type_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0,
        };

        VkSemaphoreCreateInfo sem_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &sem_type_info,
        };

        VK_CHECK(vkCreateSemaphore(ctx.device, &sem_info, nullptr, &res_system.timeline_semaphore));
    }

    void shutdown_resources()
    {
        vkDestroyDescriptorPool(ctx.device, res_system.global_pool, nullptr);
        vkDestroyDescriptorSetLayout(ctx.device, res_system.global_layout, nullptr);

        res_system.texture_heap.free_indices.clear();
        res_system.storage_image_heap.free_indices.clear();
        res_system.buffer_heap.free_indices.clear();

        vkDestroySemaphore(ctx.device, res_system.timeline_semaphore, nullptr);
    }

    void resource_system_t::process_deletions(u64_t cur_timeline_value)
    {
        std::scoped_lock lock(deletion_mutex);

        while (!deletion_queue.empty() && cur_timeline_value >= deletion_queue.front().gpu_timeline_value)
        {
            deferred_delete_t& del = deletion_queue.front();

            switch (del.type)
            {
            case smol::renderer::resource_type_e::TEXTURE:
            {
                if (del.handle.texture.view) { vkDestroyImageView(ctx.device, del.handle.texture.view, nullptr); }
                vmaDestroyImage(ctx.allocator, del.handle.texture.image, del.handle.texture.allocation);
                texture_heap.release(del.bindless_id);
                break;
            }

            case smol::renderer::resource_type_e::BUFFER:
            {
                vmaDestroyBuffer(ctx.allocator, del.handle.buffer.buffer, del.handle.buffer.allocation);
                if (del.bindless_id != NULL_HANDLE) { buffer_heap.release(del.bindless_id); }
                break;
            }

            case smol::renderer::resource_type_e::STORAGE_IMAGE:
            {
                if (del.handle.texture.view) { vkDestroyImageView(ctx.device, del.handle.texture.view, nullptr); }

                vmaDestroyImage(ctx.allocator, del.handle.texture.image, del.handle.texture.allocation);

                if (del.bindless_id != NULL_HANDLE) { storage_image_heap.release(del.bindless_id); }
                break;
            }

            case smol::renderer::resource_type_e::COMMAND_BUFFER:
            {
                vkFreeCommandBuffers(ctx.device, del.handle.cmd_buffer.pool, 1, &del.handle.cmd_buffer.cmd);
                break;
            }
            }

            deletion_queue.pop_front();
        }
    }
} // namespace smol::renderer