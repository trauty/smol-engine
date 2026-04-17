#include "imgui_backend.h"

#include "imgui/imgui.h"
#include "smol/asset.h"
#include "smol/assets/material.h"
#include "smol/assets/shader.h"
#include "smol/defines.h"
#include "smol/ecs_fwd.h"
#include "smol/hash.h"
#include "smol/math.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_resources.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/rendergraph.h"
#include "smol/rendering/vulkan.h"
#include "vulkan/vulkan_core.h"

#include <cstdint>
#include <cstring>
#include <mutex>

namespace smol::editor::imgui
{
    struct imgui_ctx_t
    {
        asset_t<shader_t> shader;
        asset_t<material_t> material;

        VkImage font_image = VK_NULL_HANDLE;
        VmaAllocation font_alloc = VK_NULL_HANDLE;
        VkImageView font_view = VK_NULL_HANDLE;
        u32_t font_tex_bindless_id = renderer::BINDLESS_NULL_HANDLE;

        VkBuffer vertex_buffer[renderer::MAX_FRAMES_IN_FLIGHT] = {VK_NULL_HANDLE};
        VmaAllocation vertex_alloc[renderer::MAX_FRAMES_IN_FLIGHT] = {VK_NULL_HANDLE};
        void* vertex_mapped[renderer::MAX_FRAMES_IN_FLIGHT] = {nullptr};
        size_t vertex_buffer_size[renderer::MAX_FRAMES_IN_FLIGHT] = {0};
        u32_t vertex_buffer_bindless_id[renderer::MAX_FRAMES_IN_FLIGHT];

        VkBuffer index_buffer[renderer::MAX_FRAMES_IN_FLIGHT] = {VK_NULL_HANDLE};
        VmaAllocation index_alloc[renderer::MAX_FRAMES_IN_FLIGHT] = {VK_NULL_HANDLE};
        void* index_mapped[renderer::MAX_FRAMES_IN_FLIGHT] = {nullptr};
        size_t index_buffer_size[renderer::MAX_FRAMES_IN_FLIGHT] = {0};

        ImDrawData* draw_data = nullptr;
    };

    static imgui_ctx_t ctx;

    void init()
    {
        for (u32_t i = 0; i < renderer::MAX_FRAMES_IN_FLIGHT; i++)
        {
            ctx.vertex_buffer_bindless_id[i] = renderer::BINDLESS_NULL_HANDLE;
        }

        ctx.shader = smol::load_asset_sync<shader_t>("engine://assets/shaders/imgui.slang");
        ctx.material = smol::load_asset_sync<material_t>("imgui_mat", ctx.shader);

        ImGuiIO& io = ImGui::GetIO();
        u8* pixels;
        i32 width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        VkDeviceSize image_size = width * height * 4;

        VkBuffer staging_buf;
        VmaAllocation staging_alloc;
        VkBufferCreateInfo staging_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = image_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        };
        VmaAllocationCreateInfo alloc_info = {
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
        VmaAllocationInfo staging_alloc_info;

        VK_CHECK(vmaCreateBuffer(renderer::ctx.allocator, &staging_info, &alloc_info, &staging_buf, &staging_alloc,
                                 &staging_alloc_info));
        std::memcpy(staging_alloc_info.pMappedData, pixels, static_cast<size_t>(image_size));

        VkImageCreateInfo image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = {static_cast<u32_t>(width), static_cast<u32_t>(height), 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
        VmaAllocationCreateInfo img_alloc_info = {.usage = VMA_MEMORY_USAGE_AUTO};

        VK_CHECK(vmaCreateImage(renderer::ctx.allocator, &image_info, &img_alloc_info, &ctx.font_image, &ctx.font_alloc,
                                nullptr));

        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = ctx.font_image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
        };

        VK_CHECK(vkCreateImageView(renderer::ctx.device, &view_info, nullptr, &ctx.font_view));

        VkCommandBuffer cmd = renderer::begin_transfer_commands();

        VkImageMemoryBarrier barrier_to_dst = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = ctx.font_image,
            .subresourceRange = view_info.subresourceRange,
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier_to_dst);

        VkBufferImageCopy copy_region = {
            .bufferOffset = 0,
            .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .mipLevel = 0,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
            .imageExtent = image_info.extent,
        };
        vkCmdCopyBufferToImage(cmd, staging_buf, ctx.font_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

        VkImageMemoryBarrier release_barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = smol::renderer::ctx.queue_fam_indices.transfer_family.value(),
            .dstQueueFamilyIndex = smol::renderer::ctx.queue_fam_indices.graphics_family.value(),
            .image = ctx.font_image,
            .subresourceRange = view_info.subresourceRange,
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &release_barrier);

        u64_t signal_value = renderer::submit_transfer_commands(cmd);

        {
            std::scoped_lock lock(renderer::res_system.pending_mutex);
            VkImageMemoryBarrier acquire_barrier = release_barrier;
            acquire_barrier.srcAccessMask = 0;
            acquire_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            renderer::res_system.pending_acquires.push_back({
                .type = renderer::resource_type_e::TEXTURE,
                .handle = {.image = ctx.font_image},
                .barrier = {.image_barrier = acquire_barrier},
            });
        }

        ctx.font_tex_bindless_id = renderer::res_system.texture_heap.acquire();

        VkDescriptorImageInfo image_desc_info = {
            .sampler = VK_NULL_HANDLE,
            .imageView = ctx.font_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        VkWriteDescriptorSet write_desc = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = renderer::res_system.global_set,
            .dstBinding = renderer::TEXTURES_BINDING_POINT,
            .dstArrayElement = ctx.font_tex_bindless_id,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &image_desc_info,
        };
        vkUpdateDescriptorSets(renderer::ctx.device, 1, &write_desc, 0, nullptr);

        io.Fonts->SetTexID((ImTextureID)(intptr_t)ctx.font_tex_bindless_id);

        {
            std::scoped_lock lock(renderer::res_system.deletion_mutex);
            renderer::res_system.deletion_queue.push_back({
                .type = renderer::resource_type_e::BUFFER,
                .handle = {.buffer = {staging_buf, staging_alloc}},
                .bindless_id = renderer::BINDLESS_NULL_HANDLE,
                .gpu_timeline_value = signal_value,
            });
        }

        renderer::register_renderer_feature(
            [](renderer::rendergraph_t& graph, ecs::registry_t& reg)
            {
                renderer::rg_pass_t& pass = graph.add_pass("ImGuiPass");

                pass.color_writes = {graph.get_resource("Swapchain")};
                pass.texture_reads.push_back(graph.get_resource("FinalOutput"));

                pass.execute_callback = [](VkCommandBuffer cmd, ecs::registry_t& reg)
                {
                    if (!ctx.draw_data || ctx.draw_data->CmdListsCount == 0) { return; }

                    if (ctx.draw_data->DisplaySize.x <= 0.0f || ctx.draw_data->DisplaySize.y <= 0.0f) { return; }

                    ImDrawData* draw_data = ctx.draw_data;
                    u32_t cur_frame = renderer::ctx.cur_frame;

                    size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
                    size_t index_size = draw_data->TotalIdxCount * sizeof(u32_t);

                    if (ctx.vertex_buffer_size[cur_frame] < vertex_size)
                    {
                        if (ctx.vertex_buffer[cur_frame])
                        {
                            vmaDestroyBuffer(renderer::ctx.allocator, ctx.vertex_buffer[cur_frame],
                                             ctx.vertex_alloc[cur_frame]);
                        }
                        VkBufferCreateInfo buf_info = {
                            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                            .size = vertex_size + 5000,
                            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                        };
                        VmaAllocationCreateInfo alloc_info = {
                            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                                     VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                            .usage = VMA_MEMORY_USAGE_AUTO,
                            .requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        };
                        VmaAllocationInfo vma_alloc_info;
                        VK_CHECK(vmaCreateBuffer(renderer::ctx.allocator, &buf_info, &alloc_info,
                                                 &ctx.vertex_buffer[cur_frame], &ctx.vertex_alloc[cur_frame],
                                                 &vma_alloc_info));
                        ctx.vertex_mapped[cur_frame] = vma_alloc_info.pMappedData;
                        ctx.vertex_buffer_size[cur_frame] = vertex_size + 5000;

                        if (ctx.vertex_buffer_bindless_id[cur_frame] == renderer::BINDLESS_NULL_HANDLE)
                        {
                            ctx.vertex_buffer_bindless_id[cur_frame] = renderer::res_system.buffer_heap.acquire();
                        }

                        VkDescriptorBufferInfo dbi = {ctx.vertex_buffer[cur_frame], 0, VK_WHOLE_SIZE};
                        VkWriteDescriptorSet write_desc = {
                            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .dstSet = renderer::res_system.global_set,
                            .dstBinding = renderer::STORAGE_BUFFERS_BINDING_POINT,
                            .dstArrayElement = ctx.vertex_buffer_bindless_id[cur_frame],
                            .descriptorCount = 1,
                            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .pBufferInfo = &dbi,
                        };
                        vkUpdateDescriptorSets(renderer::ctx.device, 1, &write_desc, 0, nullptr);
                    }

                    if (ctx.index_buffer_size[cur_frame] < index_size)
                    {
                        if (ctx.index_buffer[cur_frame])
                        {
                            vmaDestroyBuffer(renderer::ctx.allocator, ctx.index_buffer[cur_frame],
                                             ctx.index_alloc[cur_frame]);
                        }
                        VkBufferCreateInfo buf_info = {
                            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                            .size = index_size + 5000,
                            .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                        };
                        VmaAllocationCreateInfo alloc_info = {
                            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                                     VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                            .usage = VMA_MEMORY_USAGE_AUTO,
                            .requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        };
                        VmaAllocationInfo vma_alloc_info;
                        VK_CHECK(vmaCreateBuffer(renderer::ctx.allocator, &buf_info, &alloc_info,
                                                 &ctx.index_buffer[cur_frame], &ctx.index_alloc[cur_frame],
                                                 &vma_alloc_info));
                        ctx.index_mapped[cur_frame] = vma_alloc_info.pMappedData;
                        ctx.index_buffer_size[cur_frame] = index_size + 5000;
                    }

                    ImDrawVert* vtx_dst = (ImDrawVert*)ctx.vertex_mapped[cur_frame];
                    u32_t* idx_dst = (u32_t*)ctx.index_mapped[cur_frame];

                    i32 global_vtx_offset = 0;

                    for (i32 i = 0; i < draw_data->CmdListsCount; i++)
                    {
                        const ImDrawList* cmd_list = draw_data->CmdLists[i];
                        std::memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));

                        for (i32 j = 0; j < cmd_list->IdxBuffer.Size; j++)
                        {
                            idx_dst[j] = (u32_t)(cmd_list->IdxBuffer.Data[j]) + global_vtx_offset;
                        }

                        vtx_dst += cmd_list->VtxBuffer.Size;
                        idx_dst += cmd_list->IdxBuffer.Size;
                        global_vtx_offset += cmd_list->VtxBuffer.Size;
                    }

                    vec2_t scale = {
                        2.0f / draw_data->DisplaySize.x,
                        2.0f / draw_data->DisplaySize.y,
                    };
                    vec2_t translate = {
                        -1.0f - draw_data->DisplayPos.x * scale.x,
                        -1.0f - draw_data->DisplayPos.y * scale.y,
                    };

                    ctx.material->set_property("scale"_h, scale);
                    ctx.material->set_property("translate"_h, translate);
                    ctx.material->set_property("vertex_buffer_id"_h, ctx.vertex_buffer_bindless_id[cur_frame]);
                    ctx.material->sync();

                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.shader->pipeline);

                    VkDescriptorSet sets[] = {renderer::res_system.global_set, renderer::res_system.frame_set};

                    vkCmdBindDescriptorSets(
                        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.shader->pipeline_layout, 0, 2, sets, 1,
                        &renderer::ctx.per_frame_objects[renderer::ctx.cur_frame].global_data_offset);

                    vkCmdBindIndexBuffer(cmd, ctx.index_buffer[cur_frame], 0, VK_INDEX_TYPE_UINT32);

                    renderer::push_constants_t pc = {
                        .material_buffer_id = renderer::res_system.material_heap.bindless_id,
                        .custom_data = ctx.material->heap_offset[cur_frame],
                    };

                    VkViewport viewport = {0, 0, draw_data->DisplaySize.x, draw_data->DisplaySize.y, 0.0f, 1.0f};
                    vkCmdSetViewport(cmd, 0, 1, &viewport);

                    i32 global_index_offset = 0;
                    ImVec2 clip_off = draw_data->DisplayPos;

                    for (i32 i = 0; i < draw_data->CmdListsCount; i++)
                    {
                        const ImDrawList* cmd_list = draw_data->CmdLists[i];
                        for (i32 cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
                        {
                            const ImDrawCmd* cmd_ptr = &cmd_list->CmdBuffer[cmd_i];

                            ImVec2 clip_min(cmd_ptr->ClipRect.x - clip_off.x, cmd_ptr->ClipRect.y - clip_off.y);
                            ImVec2 clip_max(cmd_ptr->ClipRect.z - clip_off.x, cmd_ptr->ClipRect.w - clip_off.y);

                            clip_min.x = std::max(0.0f, clip_min.x);
                            clip_min.y = std::max(0.0f, clip_min.y);

                            float max_w = (f32)renderer::ctx.swapchain.extent.width;
                            float max_h = (f32)renderer::ctx.swapchain.extent.height;

                            clip_max.x = std::min(max_w, clip_max.x);
                            clip_max.y = std::min(max_h, clip_max.y);

                            if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) { continue; }

                            VkRect2D scissor = {
                                {(i32_t)clip_min.x,                (i32_t)clip_min.y               },
                                {(u32_t)(clip_max.x - clip_min.x), (u32_t)(clip_max.y - clip_min.y)}
                            };
                            vkCmdSetScissor(cmd, 0, 1, &scissor);

                            pc.object_buffer_id = (u32_t)(intptr_t)cmd_ptr->GetTexID();
                            vkCmdPushConstants(cmd, ctx.shader->pipeline_layout,
                                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                               sizeof(renderer::push_constants_t), &pc);
                            vkCmdDrawIndexed(cmd, cmd_ptr->ElemCount, 1, cmd_ptr->IdxOffset + global_index_offset, 0,
                                             0);
                        }

                        global_index_offset += cmd_list->IdxBuffer.Size;
                    }
                };
            });
    }

    void shutdown()
    {
        for (size_t i = 0; i < renderer::MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (ctx.vertex_buffer[i])
            {
                vmaDestroyBuffer(renderer::ctx.allocator, ctx.vertex_buffer[i], ctx.vertex_alloc[i]);
            }

            if (ctx.index_buffer[i])
            {
                vmaDestroyBuffer(renderer::ctx.allocator, ctx.index_buffer[i], ctx.index_alloc[i]);
            }

            if (ctx.vertex_buffer_bindless_id[i] != renderer::BINDLESS_NULL_HANDLE)
            {
                renderer::res_system.buffer_heap.release(ctx.vertex_buffer_bindless_id[i]);
            }
        }

        if (ctx.font_view) { vkDestroyImageView(renderer::ctx.device, ctx.font_view, nullptr); }
        if (ctx.font_image) { vmaDestroyImage(renderer::ctx.allocator, ctx.font_image, ctx.font_alloc); }

        if (ctx.font_tex_bindless_id != renderer::BINDLESS_NULL_HANDLE)
        {
            renderer::res_system.texture_heap.release(ctx.font_tex_bindless_id);
        }

        ctx.material.release();
        ctx.shader.release();
    }

    void submit(ImDrawData* draw_data) { ctx.draw_data = draw_data; }
} // namespace smol::editor::imgui