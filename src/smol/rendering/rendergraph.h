#pragma once

#include "smol/defines.h"
#include "smol/ecs_fwd.h"
#include "smol/rendering/renderer_types.h"
#include "vulkan/vulkan_core.h"

#include <functional>
#include <vector>

namespace smol::renderer
{
    using rg_resource_id = u32_t;
    constexpr rg_resource_id RG_NULL_ID = 0xffffffff;

    struct SMOL_API rg_resource_t
    {
        std::string name;
        image_desc_t desc;

        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        u32_t bindless_id = BINDLESS_NULL_HANDLE;

        VkImageLayout cur_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool is_imported = false;
    };

    using pass_callback_t = std::function<void(VkCommandBuffer, ecs::registry_t&)>;

    struct SMOL_API rg_pass_t
    {
        std::string name;
        std::vector<rg_resource_id> color_writes;
        std::vector<rg_resource_id> storage_writes;
        rg_resource_id depth_stencil = RG_NULL_ID;
        std::vector<rg_resource_id> texture_reads;

        pass_callback_t execute_callback;
    };

    class SMOL_API rendergraph_t
    {
      public:
        void clear();

        rg_resource_id create_image(const std::string& name, const image_desc_t& desc);
        rg_resource_id import_image(const std::string& name, VkImage image, VkImageView view, VkFormat format,
                                    u32_t width, u32_t height);

        rg_pass_t& add_pass(const std::string& name);

        void compile(per_frame_t& frame_data);
        void execute(VkCommandBuffer cmd, ecs::registry_t& reg);

        rg_resource_id get_resource(const std::string& name) const;
        VkImageLayout get_layout(rg_resource_id id) const;
        u32_t get_bindless_id(rg_resource_id id) const;

      private:
        std::vector<rg_resource_t> resources;
        std::vector<rg_pass_t> passes;
        std::vector<size_t> sorted_passes;
    };
} // namespace smol::renderer