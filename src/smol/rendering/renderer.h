#pragma once

#include "smol/defines.h"
#include "smol/events.h"
#include "smol/log.h"

#include <mutex>
#include <vector>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

namespace smol::renderer
{
    namespace ctx
    {
        constexpr u32 MAX_FRAMES_IN_FLIGHT = 2;
        inline u32 cur_frame = 0;

        inline VkInstance instance;
        inline VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        inline VkDevice device;
        inline VkQueue graphics_queue;
        inline VkQueue present_queue;
        inline VkSurfaceKHR surface;
        inline VkSwapchainKHR swapchain;
        inline VkFormat swapchain_format;
        inline VkExtent2D swapchain_extent;
        inline std::vector<VkImage> swapchain_images;
        inline std::vector<VkImageView> swapchain_image_views;
        inline VkRenderPass render_pass;
        inline std::vector<VkFramebuffer> framebuffers;
        inline VkCommandPool command_pool;
        inline std::vector<VkCommandBuffer> command_buffers;

        inline std::vector<VkSemaphore> image_available_semaphores;
        inline std::vector<VkSemaphore> render_finished_semaphores;
        inline std::vector<VkFence> in_flight_fences;
        inline std::vector<VkFence> images_in_flight;

        inline VkDescriptorSetLayout global_set_layout;
        inline VkDescriptorPool descriptor_pool;

        inline VkImage depth_image;
        inline VkDeviceMemory depth_image_mem;
        inline VkImageView depth_image_view;

        inline std::mutex queue_mutex;
        inline std::mutex descriptor_mutex;

        inline smol::events::subscription_id_t sub_id;
    } // namespace ctx

    enum class shader_stage_e
    {
        VERTEX,
        FRAGMENT,
        GEOMETRY, // <-- not implemented
        COMPUTE   // <-- not implemented
    };

    void init();
    void render();
    void shutdown();

    void create_swapchain(VkSwapchainKHR old_swapchain = VK_NULL_HANDLE);
    void recreate_swapchain();
    VkFormat find_depth_format();

    u32 find_mem_type(u32 type_filter, VkMemoryPropertyFlags props);

    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_props, VkBuffer& buffer,
                       VkDeviceMemory& buffer_memory);
    void create_image(u32 width, u32 height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                      VkMemoryPropertyFlags props, VkImage& image, VkDeviceMemory& image_mem);
} // namespace smol::renderer