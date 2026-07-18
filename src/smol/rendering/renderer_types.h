#pragma once

#include "smol/asset.h"
#include "smol/assets/shader.h"
#include "smol/assets/texture.h"
#include "smol/defines.h"
#include "smol/log.h"
#include "smol/math.h"
#include "smol/memory/linear_allocator.h"
#include "smol/rendering/renderer_constants.h"
#include "smol/rendering/shader_instance.h"
#include "smol/rendering/vulkan.h"
#include "vulkan/vulkan_core.h"

#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

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
    struct active_pipeline_t
    {
        VkPipeline pipeline;
        VkPipeline shadow_pipeline;
        VkPipelineLayout layout;
        u32_t pipeline_index;
        u32_t blend_sort_key;
    };

    enum class view_kind_e : u32_t
    {
        COLOR,
        DEPTH_ONLY
    };

    struct queue_family_indices_t
    {
        std::optional<u32_t> graphics_family;
        std::optional<u32_t> compute_family;
        std::optional<u32_t> transfer_family;
        std::optional<u32_t> present_family;

        bool is_complete() const
        { return graphics_family.has_value() && present_family.has_value() && transfer_family.has_value(); }
    };

    struct swapchain_t
    {
        VkSwapchainKHR handle = VK_NULL_HANDLE;
        VkFormat format;
        VkExtent2D extent;

        std::vector<VkImage> images;
        std::vector<VkImageView> views;
        std::vector<VkSemaphore> release_semaphores;

        VkImage depth_image = VK_NULL_HANDLE;
        VkImageView depth_view = VK_NULL_HANDLE;
        VmaAllocation depth_allocation = VK_NULL_HANDLE;
        VkFormat depth_format;
    };

    struct push_constants_t
    {
        VkDeviceAddress object_buffer = 0;
        VkDeviceAddress material_buffer = 0;
        u32_t custom_data = 0;
        u32_t texture_id = 0;
    };

    struct alignas(16) gpu_mat4_t
    {
        f32 data[16];
    };

    struct alignas(16) gpu_vec4_t
    {
        union
        {
            struct
            {
                f32 x, y, z, w;
            };
            f32 raw[4];
        } data;
    };

    struct alignas(16) gpu_vec3_t
    {
        union
        {
            struct
            {
                f32 x, y, z;
            };
            f32 raw[3];
        } data;
    };

    struct gpu_directional_light_t
    {
        gpu_vec4_t direction_intensity;
        gpu_vec4_t color;
    };

    struct gpu_point_light_t
    {
        gpu_vec4_t position_radius;
        gpu_vec4_t color_intensity;
    };

    struct gpu_spot_light_t
    {
        gpu_vec4_t position_range;
        gpu_vec4_t direction_intensity;
        gpu_vec4_t color_inner_cos;
        gpu_vec4_t outer_cos;
    };

    struct global_data_t
    {
        gpu_mat4_t view;
        gpu_mat4_t projection;
        gpu_mat4_t view_proj;
        gpu_vec4_t camera_pos;
        gpu_vec4_t frustum_planes[6];
        gpu_mat4_t light_view_proj = {};

        VkDeviceAddress dir_light_buffer = 0;
        VkDeviceAddress point_light_buffer = 0;
        VkDeviceAddress spot_light_buffer = 0;

        f32 time;
        u32_t dir_light_count;
        u32_t point_light_count;
        u32_t spot_light_count;
        u32_t object_count;
        u32_t active_pipeline_count;
        u32_t cull_flags;    // bit0 = shadow only view
        u32_t shadow_map_id; // bindless id of the dir shadowmap
    };

    struct object_data_t
    {
        gpu_mat4_t model_matrix;
        gpu_mat4_t normal_matrix;
        VkDeviceAddress vertex_buffer;
        VkDeviceAddress index_buffer;
        u32_t material_offset;
        u32_t index_count;
        u32_t vertex_count;
        f32 bounding_sphere_radius;
        u32_t pipeline_index;
        u32_t flags; // bit0 = shadow caster
        gpu_vec4_t bounding_sphere_center;
    };

    static_assert(sizeof(object_data_t) == 192);
    static_assert(offsetof(object_data_t, vertex_buffer) == 128);
    static_assert(offsetof(object_data_t, index_buffer) == 136);
    static_assert(offsetof(object_data_t, material_offset) == 144);
    static_assert(offsetof(object_data_t, bounding_sphere_center) == 176);

    static_assert(sizeof(push_constants_t) == 24);
    static_assert(offsetof(push_constants_t, object_buffer) == 0);
    static_assert(offsetof(push_constants_t, material_buffer) == 8);
    static_assert(offsetof(push_constants_t, custom_data) == 16);
    static_assert(offsetof(push_constants_t, texture_id) == 20);

    static_assert(offsetof(global_data_t, light_view_proj) == 304);
    static_assert(offsetof(global_data_t, dir_light_buffer) == 368);
    static_assert(offsetof(global_data_t, point_light_buffer) == 376);
    static_assert(offsetof(global_data_t, spot_light_buffer) == 384);
    static_assert(offsetof(global_data_t, time) == 392);
    static_assert(offsetof(global_data_t, shadow_map_id) == 420);

    struct image_desc_t
    {
        u32_t width;
        u32_t height;
        VkFormat format;
        VkImageUsageFlags usage;
        VkImageAspectFlags aspect;

        bool operator==(const image_desc_t& other) const
        {
            return width == other.width && height == other.height && format == other.format && usage == other.usage &&
                   aspect == other.aspect;
        }
    };

    struct transient_image_t
    {
        image_desc_t desc;
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        u32_t bindless_id = BINDLESS_NULL_HANDLE;
        bool in_use = false;
        u32_t frames_unused = 0;
    };

    struct transient_pool_t
    {
        std::deque<transient_image_t> images;

        transient_image_t* acquire(const image_desc_t& desc);
        void reset();
        void shutdown();
    };

    struct render_view_t
    {
        u32_t name_hash = 0;
        view_kind_e kind = view_kind_e::COLOR;
        mat4_t view;
        mat4_t projection;
        mat4_t view_proj;
        vec3_t position;
        u32_t color_target_hash = 0;
        u32_t depth_target_hash = 0;
        bool has_depth_desc = false;
        image_desc_t depth_desc{}; // used to create the shadowmap image
        VkExtent2D extent{};
    };

    struct output_target_t
    {
        u32_t target_hash = 0;
        VkExtent2D extent{};
    };

    struct view_gpu_resources_t
    {
        VkBuffer indirect_buffer = VK_NULL_HANDLE;
        VmaAllocation indirect_alloc = VK_NULL_HANDLE;
        VkDeviceSize indirect_size = 0;

        VkBuffer draw_counts_buffer = VK_NULL_HANDLE;
        VmaAllocation draw_counts_alloc = VK_NULL_HANDLE;
        VkDeviceSize draw_counts_size = 0;

        shader_instance_t culling_instance;
        bool culling_ready = false;

        u32_t global_data_offset = 0;
        view_kind_e kind = view_kind_e::COLOR;
    };

    struct per_frame_t
    {
        per_frame_t() = default;
        per_frame_t(const per_frame_t&) = delete;
        per_frame_t& operator=(const per_frame_t&) = delete;
        per_frame_t(per_frame_t&&) = default;
        per_frame_t& operator=(per_frame_t&&) = default;

        u64_t target_timeline_value = 0;
        VkCommandPool main_command_pool = VK_NULL_HANDLE;
        VkCommandBuffer main_command_buffer = VK_NULL_HANDLE;
        VkSemaphore swapchain_acquire_semaphore = VK_NULL_HANDLE;

        global_data_t* mapped_global_data = nullptr;
        u32_t global_data_offset = 0;

        VkBuffer object_buffer = VK_NULL_HANDLE;
        VmaAllocation object_allocation = VK_NULL_HANDLE;
        object_data_t* mapped_object_data = nullptr;
        VkDeviceAddress object_buffer_address = 0;

        u32_t object_counter = 0;

        VkBuffer indirect_buffer = VK_NULL_HANDLE;
        VmaAllocation indirect_alloc = VK_NULL_HANDLE;
        VkDeviceSize indirect_size = 0;

        VkBuffer draw_counts_buffer = VK_NULL_HANDLE;
        VmaAllocation draw_counts_alloc = VK_NULL_HANDLE;
        VkDeviceSize draw_counts_size = 0;

        std::vector<active_pipeline_t> active_pipelines;
        std::unordered_map<u32_t, std::unique_ptr<view_gpu_resources_t>> views;

        VkBuffer material_buffer = VK_NULL_HANDLE;
        VmaAllocation material_allocation = VK_NULL_HANDLE;
        u8* mapped_material_data = nullptr;

        transient_pool_t transient_pool;

        VkBuffer dir_light_buffer = VK_NULL_HANDLE;
        VmaAllocation dir_light_allocation = VK_NULL_HANDLE;
        gpu_directional_light_t* mapped_dir_lights = nullptr;
        VkDeviceAddress dir_light_buffer_address = 0;

        VkBuffer point_light_buffer = VK_NULL_HANDLE;
        VmaAllocation point_light_allocation = VK_NULL_HANDLE;
        gpu_point_light_t* mapped_point_lights = nullptr;
        VkDeviceAddress point_light_buffer_address = 0;

        VkBuffer spot_light_buffer = VK_NULL_HANDLE;
        VmaAllocation spot_light_allocation = VK_NULL_HANDLE;
        gpu_spot_light_t* mapped_spot_lights = nullptr;
        VkDeviceAddress spot_light_buffer_address = 0;

        smol::linear_allocator_t frame_allocator;
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

        VkCommandPool transfer_command_pool = VK_NULL_HANDLE;
        std::mutex transfer_mutex;

        std::vector<std::string> active_instance_exts;
        std::vector<std::string> active_device_exts;
        std::vector<std::string> active_layers;

        std::mutex descriptor_mutex;

        std::vector<per_frame_t> per_frame_objects;
        u32_t cur_frame = 0;

        VkSemaphore timeline_semaphore = VK_NULL_HANDLE;
        u64_t timeline_value = 0;

        std::vector<VkSampler> samplers;

        asset_handle_t culling_shader;
        shader_instance_t culling_instance;

        asset_handle_t tonemap_shader;
        asset_handle_t tonemap_material;

        asset_handle_t default_tex;

        VkDeviceSize global_data_aligned_size = 0;
        VkBuffer global_data_buffer = VK_NULL_HANDLE;
        VmaAllocation global_data_alloc = VK_NULL_HANDLE;
        u8* global_data_mapped = nullptr;

        VkExtent2D render_extent = {0, 0};
        VkExtent2D logical_extent = {0, 0};
        VkSurfaceTransformFlagBitsKHR cur_surface_transform;

        std::unordered_map<u32_t, u32_t> output_texture_ids;
    };

    struct bindless_limits_t
    {
        u32_t max_samplers = MAX_SAMPLERS;
        u32_t max_sampled_textures = MAX_SAMPLED_TEXTURES;
        u32_t max_storage_images = MAX_STORAGE_TEXTURES;
        u32_t material_heap_size = 24u * 1024u * 1024u;
    };

    struct context_config_t
    {
        std::string app_name = "smol-engine";
        bool enable_validation = true;
        std::vector<const char*> required_instance_exts;
        std::vector<const char*> required_device_exts;
        bindless_limits_t bindless_limits = {};
    };
} // namespace smol::renderer