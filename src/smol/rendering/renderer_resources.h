#pragma once

#include "smol/assets/mesh.h"
#include "smol/defines.h"
#include "smol/rendering/renderer_types.h"
#include "vulkan/vulkan_core.h"

#include <deque>
#include <mutex>
#include <span>
#include <vector>

namespace smol::renderer
{
    enum class resource_type_e
    {
        TEXTURE,
        BUFFER,
        STORAGE_IMAGE,
        COMMAND_BUFFER,
        PIPELINE
    };

    constexpr u32_t SAMPLERS_BINDING_POINT = 0;
    constexpr u32_t TEXTURES_BINDING_POINT = 1;
    constexpr u32_t STORAGE_IMAGES_BINDING_POINT = 2;
    constexpr u32_t STORAGE_BUFFERS_BINDING_POINT = 3;

    struct pending_resource_t
    {
        resource_type_e type;

        union
        {
            VkImage image;
            VkBuffer buffer;
        } handle;

        union
        {
            VkImageMemoryBarrier image_barrier;
            VkBufferMemoryBarrier buffer_barrier;
        } barrier;
    };

    struct deferred_delete_t
    {
        resource_type_e type;

        union
        {
            struct
            {
                VkImage image;
                VmaAllocation allocation;
                VkImageView view;
            } texture;

            struct
            {
                VkBuffer buffer;
                VmaAllocation allocation;
            } buffer;

            struct
            {
                VkCommandBuffer cmd;
                VkCommandPool pool;
            } cmd_buffer;

            struct
            {
                VkPipeline pipeline;
                VkPipelineLayout layout;
            } pipeline;
        } handle;

        u32_t bindless_id;
        u64_t gpu_timeline_value;
    };

    struct descriptor_heap_t
    {
        std::vector<u32_t> free_indices;
        u32_t capacity;
        u32_t next_unused;

        void init(u32_t max_indices);
        u32_t acquire();
        void release(u32_t index);
    };

    constexpr u32_t MATERIAL_HEAP_SIZE = 24 * 1024 * 1024;

    struct material_heap_t
    {
        u32_t capacity;
        u32_t allocated_size = 0;

        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        void* mapped_mem = nullptr;

        u32_t bindless_id = NULL_HANDLE;

        void init(u32_t max_size);
        void shutdown();

        u32_t allocate(u32_t size);

        void update(u32_t offset, const void* data, u32_t size);
    };

    struct resource_system_t
    {
        VkDescriptorSetLayout global_layout;
        VkDescriptorPool global_pool;
        VkDescriptorSet global_set;

        descriptor_heap_t buffer_heap;
        descriptor_heap_t texture_heap;
        descriptor_heap_t storage_image_heap;

        material_heap_t material_heap;

        VkSemaphore timeline_semaphore;
        u64_t timeline_value;

        std::vector<pending_resource_t> pending_acquires;
        std::mutex pending_mutex;

        std::deque<deferred_delete_t> deletion_queue;
        std::mutex deletion_mutex;

        void process_deletions(u64_t cur_timeline_value);
    };

    extern resource_system_t res_system;

    void init_resources();
    void shutdown_resources();
} // namespace smol::renderer