#pragma once

#include "smol/asset.h"
#include "smol/assets/shader.h"
#include "smol/defines.h"
#include "smol/log.h"
#include "smol/rendering/vulkan.h"

#include <deque>
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
    constexpr u32_t MAX_SAMPLED_TEXTURES = 100000;
    constexpr u32_t MAX_STORAGE_TEXTURES = 4096;
    constexpr u32_t MAX_SSBOS = 100000;
    constexpr u32_t MAX_SAMPLERS = 32;

    constexpr u32_t MAX_MATERIAL_COUNT = 4096;
    constexpr u32_t MAX_MATERIAL_BUFFER_SIZE = MAX_MATERIAL_COUNT * 512;

    constexpr u32_t BINDLESS_NULL_HANDLE = 0xffffffff;

    constexpr u32_t MAX_FRAMES_IN_FLIGHT = 2;

    enum class render_bin_e : u32_t
    {
        OPAQUE = 0,
        CUTOUT,
        TRANSPARENT_ALPHA,
        TRANSPARENT_ADD,
        TRANSPARENT_MULT,
        COUNT
    };

    constexpr u32_t MAX_BINS = static_cast<u32_t>(render_bin_e::COUNT);

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
        u32_t global_buffer_id;
        u32_t object_buffer_id;
        u32_t material_buffer_id;
        u32_t custom_data;
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

    constexpr u32_t MAX_LIGHTS = 1024;
    constexpr u32_t MAX_DIR_LIGHTS = 32;

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
        f32 time;

        u32_t dir_light_buffer_id;
        u32_t dir_light_count;

        u32_t point_light_buffer_id;
        u32_t point_light_count;

        u32_t spot_light_buffer_id;
        u32_t spot_light_count;

        u32_t indirect_buffer_ids[MAX_BINS];
        u32_t draw_count_ids[MAX_BINS];
        u32_t object_count;
    };

    struct object_data_t
    {
        gpu_mat4_t model_matrix;
        gpu_mat4_t normal_matrix;
        u32_t material_offset;
        u32_t vertex_buffer_id;
        u32_t index_buffer_id;
        u32_t index_count;
        u32_t vertex_count;
        f32 bounding_sphere_radius;
        u32_t material_type;
        u32_t bin_index;
        gpu_vec4_t bounding_sphere_center;
    };

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
    };

    struct transient_pool_t
    {
        std::deque<transient_image_t> images;

        transient_image_t* acquire(const image_desc_t& desc);
        void reset();
        void shutdown();
    };

    struct render_batch_t
    {
        VkPipeline pipeline;
        VkPipelineLayout layout;
        u32_t start_idx;
        u32_t count;
    };

    struct per_frame_t
    {
        u64_t target_timeline_value = 0;
        VkCommandPool main_command_pool = VK_NULL_HANDLE;
        VkCommandBuffer main_command_buffer = VK_NULL_HANDLE;
        VkSemaphore swapchain_acquire_semaphore = VK_NULL_HANDLE;

        VkBuffer global_buffer = VK_NULL_HANDLE;
        VmaAllocation global_allocation = VK_NULL_HANDLE;
        global_data_t* mapped_global_data = nullptr;
        u32_t global_bindless_id = BINDLESS_NULL_HANDLE;

        VkBuffer object_buffer = VK_NULL_HANDLE;
        VmaAllocation object_allocation = VK_NULL_HANDLE;
        object_data_t* mapped_object_data = nullptr;
        u32_t object_bindless_id = BINDLESS_NULL_HANDLE;

        u32_t object_counter = 0;

        VkBuffer indirect_buffers[MAX_BINS] = {};
        VmaAllocation indirect_allocations[MAX_BINS] = {};
        u32_t indirect_bindless_ids[MAX_BINS] = {};

        VkBuffer draw_count_buffers[MAX_BINS] = {};
        VmaAllocation draw_count_allocations[MAX_BINS] = {};
        u32_t draw_count_bindless_ids[MAX_BINS] = {};

        VkBuffer material_buffer = VK_NULL_HANDLE;
        VmaAllocation material_allocation = VK_NULL_HANDLE;
        u8* mapped_material_data = nullptr;
        u32_t material_bindless_id = BINDLESS_NULL_HANDLE;

        transient_pool_t transient_pool;

        VkBuffer dir_light_buffer = VK_NULL_HANDLE;
        VmaAllocation dir_light_allocation = VK_NULL_HANDLE;
        gpu_directional_light_t* mapped_dir_lights = nullptr;
        u32_t dir_light_bindless_id = BINDLESS_NULL_HANDLE;

        VkBuffer point_light_buffer = VK_NULL_HANDLE;
        VmaAllocation point_light_allocation = VK_NULL_HANDLE;
        gpu_point_light_t* mapped_point_lights = nullptr;
        u32_t point_light_bindless_id = BINDLESS_NULL_HANDLE;

        VkBuffer spot_light_buffer = VK_NULL_HANDLE;
        VmaAllocation spot_light_allocation = VK_NULL_HANDLE;
        gpu_spot_light_t* mapped_spot_lights = nullptr;
        u32_t spot_light_bindless_id = BINDLESS_NULL_HANDLE;
    };

    struct registered_shader_t
    {
        asset_t<shader_t> shader;
        u32_t type_id;
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

        asset_t<shader_t> culling_shader;

        asset_t<shader_t> opaque_uber_shader;
        asset_t<shader_t> cutout_uber_shader;
        asset_t<shader_t> transparent_alpha_uber_shader;
        asset_t<shader_t> transparent_add_uber_shader;
        asset_t<shader_t> transparent_mult_uber_shader;

        std::unordered_map<std::string, registered_shader_t> shader_registry;
    };

    struct context_config_t
    {
        std::string app_name = "smol-engine";
        bool enable_validation = true;
        std::vector<const char*> required_instance_exts;
        std::vector<const char*> required_device_exts;
    };
} // namespace smol::renderer