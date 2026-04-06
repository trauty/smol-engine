#pragma once

#include "smol/asset.h"
#include "smol/assets/material.h"
#include "smol/assets/shader.h"
#include "smol/defines.h"
#include "smol/ecs_fwd.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/rendergraph.h"
#include "smol/window.h"

#include <functional>
#include <vector>

namespace smol::renderer
{
    using graph_builder_func_t = void (*)(rendergraph_t&, ecs::registry_t&);

    SMOL_API void register_renderer_feature(graph_builder_func_t builder);

    SMOL_API rg_pass_t& add_mesh_pass(rendergraph_t& graph, const std::string& name, const std::string& target_pass_tag,
                                      const std::vector<rg_resource_id>& reads,
                                      const std::vector<rg_resource_id>& writes, rg_resource_id depth);

    SMOL_API rg_pass_t&
    add_fullscreen_pass(rendergraph_t& graph, const std::string& name, asset_t<smol::material_t> material,
                        const std::vector<rg_resource_id>& reads, const std::vector<rg_resource_id>& writes,
                        std::function<void(rendergraph_t&, smol::material_t&)> on_execute = nullptr);

    SMOL_API rg_pass_t& add_compute_pass(rendergraph_t& graph, const std::string& name,
                                         asset_t<smol::material_t> material, u32_t dispatch_x, u32_t dispatch_y,
                                         u32_t dispatch_z, const std::vector<rg_resource_id>& reads,
                                         const std::vector<rg_resource_id>& writes,
                                         std::function<void(rendergraph_t&, smol::material_t&)> on_execute = nullptr);

    bool init(const context_config_t& config, SDL_Window* window);
    void reset_assets();
    void shutdown();

    extern render_context_t ctx;

    void render(ecs::registry_t& reg);

    void init_per_frame(per_frame_t& frame_data);
    void shutdown_per_frame(per_frame_t& frame_data);

    bool resize(const u32_t width, const u32_t height);
    void init_swapchain();
    VkResult acquire_next_image(u32_t* image);
    VkFormat find_depth_format();
    VkSurfaceFormatKHR select_surface_format(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                             std::vector<VkFormat> const& preferred_formats = {
                                                 VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
                                                 VK_FORMAT_A8B8G8R8_SRGB_PACK32});

    void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
                          VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT);

    void create_buffer(VkDeviceSize size, VkBufferUsageFlags buffer_usage, VmaMemoryUsage mem_usage, VkBuffer& buffer,
                       VmaAllocation& allocation);
    void create_image(u32 width, u32 height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                      VkMemoryPropertyFlags props, VkImage& image, VkDeviceMemory& image_mem);

    VkCommandBuffer begin_transfer_commands();
    u64_t submit_transfer_commands(VkCommandBuffer cmd);

    VkSampler create_sampler(VkFilter filter, VkSamplerAddressMode address_mode);

    SMOL_API void register_custom_shader(asset_t<shader_t> shader);

    SMOL_API void set_render_resolution(u32_t width, u32_t height);
    SMOL_API u32_t get_viewport_texture_id();

    SMOL_API void set_use_offscreen_viewport(bool value);
} // namespace smol::renderer