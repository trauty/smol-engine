#include "renderer.h"

#include "smol/asset/mesh.h"
#include "smol/asset/shader.h"
#include "smol/components/camera.h"
#include "smol/components/renderer.h"
#include "smol/components/transform.h"
#include "smol/core/gameobject.h"
#include "smol/defines.h"
#include "smol/log.h"
#include "smol/math_util.h"
#include "smol/rendering/material.h"

#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cglm/mat4.h>
#include <cglm/vec3.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

using namespace smol::components;

namespace smol::renderer
{
    namespace
    {
        std::vector<GLuint> all_shader_programs;

        struct alignas(32) global_data_t
        {
            mat4 smol_view;
            mat4 smol_projection;
            vec3 smol_camera_position;
            f32 padding0;
            vec3 smol_camera_direction;
            f32 padding1;
        };

        struct frame_data_t
        {
            VkBuffer global_data_buffer;
            VkDeviceMemory global_data_mem;
            void* global_data_mapped_mem;
            VkDescriptorSet global_descriptor;
        };

        std::vector<frame_data_t> frames;

        struct object_push_constants_t
        {
            mat4 smol_model_matrix;
        };
    } // namespace

    void init()
    {
        frames.resize(ctx::MAX_FRAMES_IN_FLIGHT);

        for (u32 i = 0; i < ctx::MAX_FRAMES_IN_FLIGHT; i++)
        {
            create_buffer(sizeof(global_data_t), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          frames[i].global_data_buffer, frames[i].global_data_mem);
            vkMapMemory(ctx::device, frames[i].global_data_mem, 0, sizeof(global_data_t), 0,
                        &frames[i].global_data_mapped_mem);

            VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            alloc_info.pNext = nullptr;
            alloc_info.descriptorPool = ctx::descriptor_pool;
            alloc_info.descriptorSetCount = 1;
            alloc_info.pSetLayouts = &ctx::global_set_layout;
            VK_CHECK(vkAllocateDescriptorSets(ctx::device, &alloc_info, &frames[i].global_descriptor));

            VkDescriptorBufferInfo buffer_info = {};
            buffer_info.buffer = frames[i].global_data_buffer;
            buffer_info.offset = 0;
            buffer_info.range = sizeof(global_data_t);

            VkWriteDescriptorSet descriptor_write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            descriptor_write.pNext = nullptr;
            descriptor_write.dstSet = frames[i].global_descriptor;
            descriptor_write.dstBinding = 0;
            descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptor_write.descriptorCount = 1;
            descriptor_write.pBufferInfo = &buffer_info;
            vkUpdateDescriptorSets(ctx::device, 1, &descriptor_write, 0, nullptr);
        }
    }

    void render()
    {
        if (camera_ct::main_camera == nullptr) { return; }

        vkWaitForFences(ctx::device, 1, &ctx::in_flight_fences[ctx::cur_frame], VK_TRUE, UINT64_MAX);

        u32 image_index = 0;
        VkResult res =
            vkAcquireNextImageKHR(ctx::device, ctx::swapchain, UINT64_MAX,
                                  ctx::image_available_semaphores[ctx::cur_frame], VK_NULL_HANDLE, &image_index);

        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) { return; }

        if (ctx::images_in_flight[image_index] != VK_NULL_HANDLE)
        {
            vkWaitForFences(ctx::device, 1, &ctx::images_in_flight[image_index], VK_TRUE, UINT64_MAX);
        }

        ctx::images_in_flight[image_index] = ctx::in_flight_fences[ctx::cur_frame];

        vkResetFences(ctx::device, 1, &ctx::in_flight_fences[ctx::cur_frame]);
        VkCommandBuffer cmd = ctx::command_buffers[ctx::cur_frame];
        vkResetCommandBuffer(cmd, 0);

        camera_ct* cam = camera_ct::main_camera;
        global_data_t global_data;
        glm_mat4_copy(cam->view_matrix.data, global_data.smol_view);
        glm_mat4_copy(cam->projection_matrix.data, global_data.smol_projection);
        glm_vec3_copy(cam->transform->get_world_position().data, global_data.smol_camera_position);
        glm_vec3_copy(cam->transform->get_world_euler_angles().data, global_data.smol_camera_direction);

        std::memcpy(frames[ctx::cur_frame].global_data_mapped_mem, &global_data, sizeof(global_data_t));

        VkCommandBufferBeginInfo cmd_begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        cmd_begin_info.pNext = nullptr;
        vkBeginCommandBuffer(cmd, &cmd_begin_info);

        VkRenderPassBeginInfo rp_begin_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp_begin_info.pNext = nullptr;
        rp_begin_info.renderPass = ctx::render_pass;
        rp_begin_info.framebuffer = ctx::framebuffers[image_index];
        rp_begin_info.renderArea.offset = {0, 0};
        rp_begin_info.renderArea.extent = ctx::swapchain_extent;

        VkClearValue clear_values[2] = {};
        clear_values[0].color = {{0.01f, 0.01f, 0.01f, 1.0f}};
        clear_values[1].depthStencil = {1.0f, 0};

        rp_begin_info.clearValueCount = 2;
        rp_begin_info.pClearValues = clear_values;

        vkCmdBeginRenderPass(cmd, &rp_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (f32)ctx::swapchain_extent.width;
        viewport.height = (f32)ctx::swapchain_extent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = {};
        scissor.offset = {0, 0};
        scissor.extent = ctx::swapchain_extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        for (const renderer_ct* renderer : renderer_ct::all_renderers)
        {
            if (!renderer->is_active()) { continue; }

            const material_t& mat = renderer->material;
            const mesh_asset_t& mesh = renderer->mesh;

            if (!mat.shader.ready() || !mesh.ready()) { continue; }

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mat.shader.shader_data->pipeline);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mat.shader.shader_data->pipeline_layout, 0, 1,
                                    &frames[ctx::cur_frame].global_descriptor, 0, nullptr);

            if (mat.descriptor_set != VK_NULL_HANDLE)
            {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mat.shader.shader_data->pipeline_layout,
                                        1, 1, &mat.descriptor_set, 0, nullptr);
            }

            object_push_constants_t push;
            mat4_t& model_mat = renderer->get_gameobject()->get_transform()->get_world_matrix();
            glm_mat4_copy(model_mat.data, push.smol_model_matrix);
            vkCmdPushConstants(cmd, mat.shader.shader_data->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(object_push_constants_t), &push);

            VkBuffer vertex_buffers[] = {mesh.mesh_data->vertex_buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);

            vkCmdBindIndexBuffer(cmd, mesh.mesh_data->index_buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(cmd, mesh.index_count, 1, 0, 0, 0);
        }

        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("RENDERER", "Failed to record command buffer");
            return;
        }

        VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};

        VkSemaphore wait_semaphores[] = {ctx::image_available_semaphores[ctx::cur_frame]};
        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = wait_semaphores;
        submit_info.pWaitDstStageMask = wait_stages;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd;

        VkSemaphore signal_semaphores[] = {ctx::render_finished_semaphores[image_index]};
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = signal_semaphores;

        if (vkQueueSubmit(ctx::graphics_queue, 1, &submit_info, ctx::in_flight_fences[ctx::cur_frame]) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("RENDERER", "Failed to submit draw command buffer");
            return;
        }

        VkPresentInfoKHR present_info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present_info.pNext = nullptr;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = signal_semaphores;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &ctx::swapchain;
        present_info.pImageIndices = &image_index;

        res = vkQueuePresentKHR(ctx::present_queue, &present_info);

        if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
        {
            // handle resize
        }
        else if (res != VK_SUCCESS) { SMOL_LOG_ERROR("RENDERER", "Failed to present swapchain image"); }

        ctx::cur_frame = (ctx::cur_frame + 1) % ctx::MAX_FRAMES_IN_FLIGHT;
    }

    void shutdown()
    {
        vkDeviceWaitIdle(ctx::device);

        for (frame_data_t frame : frames)
        {
            if (frame.global_data_buffer) { vkDestroyBuffer(ctx::device, frame.global_data_buffer, nullptr); }
            if (frame.global_data_mem)
            {
                vkUnmapMemory(ctx::device, frame.global_data_mem);
                vkFreeMemory(ctx::device, frame.global_data_mem, nullptr);
            }
        }

        if (ctx::global_set_layout) { vkDestroyDescriptorSetLayout(ctx::device, ctx::global_set_layout, nullptr); }
        if (ctx::descriptor_pool) { vkDestroyDescriptorPool(ctx::device, ctx::descriptor_pool, nullptr); }

        if (ctx::depth_image_view) { vkDestroyImageView(ctx::device, ctx::depth_image_view, nullptr); }
        if (ctx::depth_image) { vkDestroyImage(ctx::device, ctx::depth_image, nullptr); }
        if (ctx::depth_image_mem) { vkFreeMemory(ctx::device, ctx::depth_image_mem, nullptr); }

        for (VkImageView view : ctx::swapchain_image_views) { vkDestroyImageView(ctx::device, view, nullptr); }
        for (VkFramebuffer fb : ctx::framebuffers) { vkDestroyFramebuffer(ctx::device, fb, nullptr); }

        if (ctx::render_pass) { vkDestroyRenderPass(ctx::device, ctx::render_pass, nullptr); }
        if (ctx::swapchain) { vkDestroySwapchainKHR(ctx::device, ctx::swapchain, nullptr); }

        for (u32 i = 0; i < ctx::MAX_FRAMES_IN_FLIGHT; i++)
        {
            vkDestroySemaphore(ctx::device, ctx::image_available_semaphores[i], nullptr);
            vkDestroyFence(ctx::device, ctx::in_flight_fences[i], nullptr);
        }

        for (u32 i = 0; i < ctx::swapchain_images.size(); i++)
        {
            vkDestroySemaphore(ctx::device, ctx::render_finished_semaphores[i], nullptr);
        }

        if (ctx::command_pool) { vkDestroyCommandPool(ctx::device, ctx::command_pool, nullptr); }

        if (ctx::device) { vkDestroyDevice(ctx::device, nullptr); }
        if (ctx::surface) { vkDestroySurfaceKHR(ctx::instance, ctx::surface, nullptr); }
    }

    u32 find_mem_type(u32 type_filter, VkMemoryPropertyFlags props)
    {
        VkPhysicalDeviceMemoryProperties mem_props;
        vkGetPhysicalDeviceMemoryProperties(ctx::physical_device, &mem_props);
        for (u32 i = 0; i < mem_props.memoryTypeCount; i++)
        {
            if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & props) == props) { return i; }
        }

        SMOL_LOG_FATAL("RENDERER", "Failed to load suitable Vulkan memory type");
        return -1;
    }

    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem_props, VkBuffer& buffer,
                       VkDeviceMemory& buffer_memory)
    {
        VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // should be a parameter
        buffer_info.pNext = nullptr;

        VK_CHECK(vkCreateBuffer(ctx::device, &buffer_info, nullptr, &buffer));

        VkMemoryRequirements mem_reqs;
        vkGetBufferMemoryRequirements(ctx::device, buffer, &mem_reqs);

        VkMemoryAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = find_mem_type(mem_reqs.memoryTypeBits, mem_props);

        VK_CHECK(vkAllocateMemory(ctx::device, &alloc_info, nullptr, &buffer_memory));
        VK_CHECK(vkBindBufferMemory(ctx::device, buffer, buffer_memory, 0));
    }

    void create_image(u32 width, u32 height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                      VkMemoryPropertyFlags props, VkImage& image, VkDeviceMemory& image_mem)
    {
        VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image_info.pNext = nullptr;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent.width = width;
        image_info.extent.height = height;
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = format;
        image_info.tiling = tiling;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = usage;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(ctx::device, &image_info, nullptr, &image) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("VULKAN", "Failed to create image; Width: {}, Height: {}", width, height);
            return;
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(ctx::device, image, &mem_reqs);

        VkMemoryAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc_info.pNext = nullptr;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex = find_mem_type(mem_reqs.memoryTypeBits, props);

        if (vkAllocateMemory(ctx::device, &alloc_info, nullptr, &image_mem) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("VULKAN", "Failed to allocate memory for image; Width: {}, Height: {}", width, height);
            return;
        }

        vkBindImageMemory(ctx::device, image, image_mem, 0);
    }
} // namespace smol::renderer