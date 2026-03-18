#include "rendergraph.h"

#include "smol/defines.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_resources.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/vulkan.h"
#include "vulkan/vulkan_core.h"

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>
#include <vector>

namespace smol::renderer
{
    void rendergraph_t::clear()
    {
        resources.clear();
        passes.clear();
    }

    rg_resource_id rendergraph_t::create_image(const std::string& name, const image_desc_t& desc)
    {
        rg_resource_id id = static_cast<rg_resource_id>(resources.size());
        resources.push_back(
            {name, desc, VK_NULL_HANDLE, VK_NULL_HANDLE, BINDLESS_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, false});
        return id;
    }

    rg_resource_id rendergraph_t::import_image(const std::string& name, VkImage image, VkImageView view,
                                               VkFormat format, u32_t width, u32_t height)
    {
        rg_resource_id id = static_cast<rg_resource_id>(resources.size());
        image_desc_t desc = {width, height, format, 0, VK_IMAGE_ASPECT_COLOR_BIT};
        resources.push_back({name, desc, image, view, BINDLESS_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, true});
        return id;
    }

    rg_pass_t& rendergraph_t::add_pass(const std::string& name)
    {
        passes.push_back({name});
        return passes.back();
    }

    void rendergraph_t::compile(per_frame_t& frame_data)
    {
        for (rg_resource_t& res : resources)
        {
            if (res.is_imported) { continue; }

            transient_image_t* transient = frame_data.transient_pool.acquire(res.desc);
            res.image = transient->image;
            res.view = transient->view;
            res.bindless_id = transient->bindless_id;
        }
    }

    void rendergraph_t::execute(VkCommandBuffer cmd, ecs::registry_t& reg)
    {
        for (const rg_pass_t& pass : passes)
        {
#ifdef SMOL_ENABLE_PROFILING
            TracyVkZone(tracy_vk_ctx, cmd, pass.name.c_str());
#endif
            bool is_compute = pass.color_writes.empty() && pass.depth_stencil == RG_NULL_ID;

            for (rg_resource_id id : pass.storage_writes)
            {
                rg_resource_t& res = resources[id];
                if (res.cur_layout != VK_IMAGE_LAYOUT_GENERAL)
                {
                    transition_image(cmd, res.image, res.cur_layout, VK_IMAGE_LAYOUT_GENERAL, res.desc.aspect);
                    res.cur_layout = VK_IMAGE_LAYOUT_GENERAL;
                }
            }

            std::vector<bool> color_needs_clear(pass.color_writes.size(), false);
            for (size_t i = 0; i < pass.color_writes.size(); i++)
            {
                rg_resource_t& res = resources[pass.color_writes[i]];

                if (res.cur_layout == VK_IMAGE_LAYOUT_UNDEFINED) { color_needs_clear[i] = true; }

                if (res.cur_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                {
                    transition_image(cmd, res.image, res.cur_layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                     res.desc.aspect);
                    res.cur_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                }
            }

            bool depth_needs_clear = true;
            if (pass.depth_stencil != RG_NULL_ID)
            {
                rg_resource_t& res = resources[pass.depth_stencil];

                if (res.cur_layout == VK_IMAGE_LAYOUT_UNDEFINED) { depth_needs_clear = true; }

                if (res.cur_layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
                {
                    transition_image(cmd, res.image, res.cur_layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                     res.desc.aspect);
                    res.cur_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                }
            }

            for (rg_resource_id id : pass.texture_reads)
            {
                rg_resource_t& res = resources[id];
                if (res.cur_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                {
                    transition_image(cmd, res.image, res.cur_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                     res.desc.aspect);
                    res.cur_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
            }

            if (!is_compute)
            {
                std::vector<VkRenderingAttachmentInfo> color_attachments;
                u32_t render_width = 0;
                u32_t render_height = 0;

                for (size_t i = 0; i < pass.color_writes.size(); i++)
                {
                    rg_resource_t& res = resources[pass.color_writes[i]];
                    render_width = res.desc.width;
                    render_height = res.desc.height;

                    VkClearValue clear_color = {
                        {0.01f, 0.01f, 0.01f, 1.0f}
                    };

                    color_attachments.push_back({
                        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .imageView = res.view,
                        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .loadOp = color_needs_clear[i] ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .clearValue = clear_color,
                    });
                }

                VkRenderingAttachmentInfo depth_attachment = {};
                if (pass.depth_stencil != RG_NULL_ID)
                {
                    rg_resource_t& res = resources[pass.depth_stencil];
                    render_width = res.desc.width;
                    render_height = res.desc.height;

                    VkClearValue clear_depth = {
                        .depthStencil = {1.0f, 0}
                    };

                    depth_attachment = {
                        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .imageView = res.view,
                        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .clearValue = clear_depth,
                    };
                }

                VkRenderingInfo rendering_info = {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .renderArea = {.extent = {render_width, render_height}},
                    .layerCount = 1,
                    .colorAttachmentCount = static_cast<u32_t>(color_attachments.size()),
                    .pColorAttachments = color_attachments.empty() ? nullptr : color_attachments.data(),
                    .pDepthAttachment = (pass.depth_stencil != RG_NULL_ID) ? &depth_attachment : nullptr,
                };

                vkCmdBeginRendering(cmd, &rendering_info);

                VkViewport vp = {
                    .width = static_cast<f32>(render_width),
                    .height = static_cast<f32>(render_height),
                    .minDepth = 0.0f,
                    .maxDepth = 1.0f,
                };
                vkCmdSetViewport(cmd, 0, 1, &vp);

                VkRect2D scissor = {
                    .extent = {render_width, render_height}
                };
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                if (pass.execute_callback) { pass.execute_callback(cmd, reg); }

                vkCmdEndRendering(cmd);
            }
            else
            {
                if (pass.execute_callback) { pass.execute_callback(cmd, reg); }
            }
        }
    }

    rg_resource_id rendergraph_t::get_resource(const std::string& name) const
    {
        for (u32_t i = 0; i < resources.size(); i++)
        {
            if (resources[i].name == name) { return i; }
        }

        return RG_NULL_ID;
    }

    VkImageLayout rendergraph_t::get_layout(rg_resource_id id) const { return resources[id].cur_layout; }

    u32_t rendergraph_t::get_bindless_id(rg_resource_id id) const { return resources[id].bindless_id; }
} // namespace smol::renderer