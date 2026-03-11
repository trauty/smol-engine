#pragma once

#include "smol/defines.h"
#include "smol/log.h"
#include "smol/math.h"
#include "vulkan/vulkan_core.h"

#include <mutex>
#include <optional>
#include <vector>
#include <vma/vk_mem_alloc.h>

#define VK_CHECK(x)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        VkResult err = x;                                                                                              \
        if (err)                                                                                                       \
        {                                                                                                              \
            SMOL_LOG_FATAL("VULKAN", "Error: {}", (int)err);                                                           \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)

namespace smol::renderer
{
    constexpr u32_t MAX_SAMPLED_TEXTURES = 100000;
    constexpr u32_t MAX_STORAGE_TEXTURES = 4096;
    constexpr u32_t MAX_SSBOS = 100000;
    constexpr u32_t MAX_SAMPLERS = 32;

    constexpr u32_t MAX_FRAMES_IN_FLIGHT = 2;

    struct queue_family_indices_t
    {
        std::optional<u32_t> graphics_family;
        std::optional<u32_t> compute_family;
        std::optional<u32_t> transfer_family;
        std::optional<u32_t> present_family;

        bool is_complete() const
        {
            return graphics_family.has_value() && present_family.has_value() && transfer_family.has_value();
        }
    };

    struct swapchain_t
    {
        VkSwapchainKHR handle = VK_NULL_HANDLE;
        VkFormat format;
        VkExtent2D extent;

        std::vector<VkImage> images;
        std::vector<VkImageView> views;
        std::vector<VkFramebuffer> framebuffers;

        VkImage depth_image = VK_NULL_HANDLE;
        VkImageView depth_view = VK_NULL_HANDLE;
        VmaAllocation depth_allocation = VK_NULL_HANDLE;
        VkFormat depth_format;
    };

    struct per_frame_t
    {
        VkFence queue_submit_fence = VK_NULL_HANDLE;
        VkCommandPool main_command_pool = VK_NULL_HANDLE;
        VkCommandBuffer main_command_buffer = VK_NULL_HANDLE;
        VkSemaphore swapchain_acquire_semaphore = VK_NULL_HANDLE;
        VkSemaphore swapchain_release_semaphore = VK_NULL_HANDLE;
    };

    struct render_context_t
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;

        swapchain_t swapchain;

        VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;

        VmaAllocator allocator = VK_NULL_HANDLE;

        VkQueue graphics_queue = VK_NULL_HANDLE;
        VkQueue present_queue = VK_NULL_HANDLE;
        VkQueue transfer_queue = VK_NULL_HANDLE;

        queue_family_indices_t queue_fam_indices;
        u32_t transfer_queue_index;
        VkPhysicalDeviceProperties properties;

        // VkCommandPool command_pool = VK_NULL_HANDLE;
        // std::vector<VkCommandBuffer> command_buffers;

        std::vector<std::string> active_instance_exts;
        std::vector<std::string> active_device_exts;
        std::vector<std::string> active_layers;

        // temporary
        VkDescriptorSetLayout global_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

        // VERY temporary
        VkRenderPass main_render_pass = VK_NULL_HANDLE;

        std::mutex descriptor_mutex;

        std::vector<per_frame_t> per_frame_objects;
        std::vector<VkSemaphore> recycled_semaphores;
        u32_t cur_frame_index = 0;

        u32_t cur_frame = 0;
    };

    struct context_config_t
    {
        std::string app_name = "smol-engine";
        bool enable_validation = true;
        std::vector<const char*> required_instance_exts;
        std::vector<const char*> required_device_exts;
    };

    struct texture_handle_t
    {
        u32_t index = 0;
        bool is_valid() const { return index != 0; }
    };

    struct gpu_object_data_t
    {
        mat4_t model_matrix;
        mat4_t normal_matrix;
        u32_t material_idx;
        u32_t _pad[3];
    };

    // missing reflection
    struct gpu_material_data_t
    {
        vec4_t color;
        u32_t albedo_tex_idx;
        u32_t _pad[3];
    };

    struct gpu_global_data_t
    {
        mat4_t view;
        mat4_t projection;
        mat4_t view_proj;
        vec4_t camera_pos;
        f32 time;
        f32 _pad[3];
    };

    struct gpu_light_t
    {
        vec4_t position_radius;  // xyz position, w radius
        vec4_t color_intensity;  // rgb color, w intensity
        vec4_t direction_cutoff; // xyz direction, w cutoff
    };
} // namespace smol::renderer