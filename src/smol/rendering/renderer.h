#pragma once

#include "smol/defines.h"
#include "smol/ecs.h"
#include "smol/rendering/renderer_types.h"
#include "smol/window.h"

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

namespace smol::renderer
{
    bool init(const context_config_t& config, SDL_Window* window);
    void shutdown();

    extern render_context_t ctx;

    void render(ecs::registry_t& reg);

    void init_per_frame(per_frame_t& frame_data);
    void shutdown_per_frame(per_frame_t& frame_data);

    bool resize(const u32_t width, const u32_t height);
    void init_swapchain();
    VkResult acquire_next_image(u32_t* image);
    VkResult present_image(u32_t image);
    VkFormat find_depth_format();
    VkSurfaceFormatKHR select_surface_format(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                             std::vector<VkFormat> const& preferred_formats = {
                                                 VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
                                                 VK_FORMAT_A8B8G8R8_SRGB_PACK32});

    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_props, VkBuffer& buffer,
                       VkDeviceMemory& buffer_memory);
    void create_image(u32 width, u32 height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                      VkMemoryPropertyFlags props, VkImage& image, VkDeviceMemory& image_mem);
} // namespace smol::renderer