#include "mesh.h"

#include "smol/asset.h"
#include "smol/assets/mesh_format.h"
#include "smol/assets/shader.h"
#include "smol/log.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_resources.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/vulkan.h"

#include <fstream>
#include <mutex>
#include <optional>
#include <tinygltf/tiny_gltf.h>
#include <vector>

namespace smol
{
    std::optional<mesh_t> asset_loader_t<mesh_t>::load(const std::string& path)
    {
        std::string cooked_path = get_cooked_path(path, ".smolmesh");

        std::ifstream file(cooked_path, std::ios::binary);
        if (!file.is_open())
        {
            SMOL_LOG_ERROR("MESH", "Mesh not found: {}", cooked_path);
            return std::nullopt;
        }

        mesh_header_t header;
        file.read(reinterpret_cast<char*>(&header), sizeof(mesh_header_t));

        if (header.magic != SMOL_MESH_MAGIC)
        {
            SMOL_LOG_ERROR("MESH", "Invalid .smolmesh file");
            return std::nullopt;
        }

        mesh_t asset = {
            .vertex_count = header.vertex_count,
            .index_count = header.index_count,
            .local_center = header.local_center,
            .local_radius = header.local_radius,
        };

        VkDeviceSize vertex_size = asset.vertex_count * sizeof(vertex_t);
        VkDeviceSize index_size = asset.index_count * sizeof(u32);
        VkDeviceSize total_staging_size = vertex_size + index_size;

        VkBuffer staging_buf;
        VmaAllocation staging_alloc;

        VkBufferCreateInfo staging_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = total_staging_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        };
        VmaAllocationCreateInfo alloc_info = {
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
        VmaAllocationInfo staging_alloc_info;

        VK_CHECK(vmaCreateBuffer(renderer::ctx.allocator, &staging_info, &alloc_info, &staging_buf, &staging_alloc,
                                 &staging_alloc_info));

        file.read(reinterpret_cast<char*>(staging_alloc_info.pMappedData), vertex_size);
        file.read(reinterpret_cast<char*>((u8*)staging_alloc_info.pMappedData + vertex_size), index_size);

        VkBufferCreateInfo mesh_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = vertex_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        };
        VmaAllocationCreateInfo mesh_alloc_info = {
            .usage = VMA_MEMORY_USAGE_AUTO,
        };

        VK_CHECK(vmaCreateBuffer(renderer::ctx.allocator, &mesh_info, &mesh_alloc_info, &asset.vertex_buffer,
                                 &asset.vertex_allocation, nullptr));

        if (index_size > 0)
        {
            mesh_info.size = index_size;
            mesh_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            VK_CHECK(vmaCreateBuffer(renderer::ctx.allocator, &mesh_info, &mesh_alloc_info, &asset.index_buffer,
                                     &asset.index_allocation, nullptr));
        }

        VkCommandBuffer cmd_buf = renderer::begin_transfer_commands();

        VkBufferCopy copy_region = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = vertex_size,
        };
        vkCmdCopyBuffer(cmd_buf, staging_buf, asset.vertex_buffer, 1, &copy_region);

        if (index_size > 0)
        {
            copy_region.srcOffset = vertex_size;
            copy_region.size = index_size;
            vkCmdCopyBuffer(cmd_buf, staging_buf, asset.index_buffer, 1, &copy_region);
        }

        std::vector<VkBufferMemoryBarrier> barriers;
        VkBufferMemoryBarrier base_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = 0,
            .srcQueueFamilyIndex = renderer::ctx.queue_fam_indices.transfer_family.value(),
            .dstQueueFamilyIndex = renderer::ctx.queue_fam_indices.graphics_family.value(),
        };

        base_barrier.buffer = asset.vertex_buffer;
        base_barrier.size = vertex_size;
        barriers.push_back(base_barrier);

        if (index_size > 0)
        {
            base_barrier.buffer = asset.index_buffer;
            base_barrier.size = index_size;
            barriers.push_back(base_barrier);
        }

        vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                             nullptr, static_cast<u32_t>(barriers.size()), barriers.data(), 0, nullptr);

        u64_t signal_value = renderer::submit_transfer_commands(cmd_buf);

        {
            std::scoped_lock lock(renderer::res_system.pending_mutex);
            for (VkBufferMemoryBarrier& barrier : barriers)
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

                renderer::res_system.pending_acquires.push_back({
                    .type = renderer::resource_type_e::BUFFER,
                    .handle = {.buffer = barrier.buffer},
                    .barrier = {.buffer_barrier = barrier},
                });
            }
        }

        asset.vertex_bindless_id = renderer::res_system.buffer_heap.acquire();
        VkDescriptorBufferInfo vertex_info = {.buffer = asset.vertex_buffer, .offset = 0, .range = VK_WHOLE_SIZE};
        VkWriteDescriptorSet vertex_write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = renderer::res_system.global_set,
            .dstBinding = renderer::STORAGE_BUFFERS_BINDING_POINT,
            .dstArrayElement = asset.vertex_bindless_id,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &vertex_info,
        };
        vkUpdateDescriptorSets(renderer::ctx.device, 1, &vertex_write, 0, nullptr);

        if (index_size > 0)
        {
            asset.index_bindless_id = renderer::res_system.buffer_heap.acquire();
            VkDescriptorBufferInfo index_info = {.buffer = asset.index_buffer, .offset = 0, .range = VK_WHOLE_SIZE};
            VkWriteDescriptorSet index_write = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = renderer::res_system.global_set,
                .dstBinding = renderer::STORAGE_BUFFERS_BINDING_POINT,
                .dstArrayElement = asset.index_bindless_id,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &index_info,
            };
            vkUpdateDescriptorSets(renderer::ctx.device, 1, &index_write, 0, nullptr);
        }

        {
            std::scoped_lock lock(renderer::res_system.deletion_mutex);
            renderer::res_system.deletion_queue.push_back({
                .type = renderer::resource_type_e::BUFFER,
                .handle = {.buffer = {staging_buf, staging_alloc}},
                .bindless_id = renderer::BINDLESS_NULL_HANDLE,
                .gpu_timeline_value = signal_value,
            });
        }

        return asset;
    }

    void asset_loader_t<mesh_t>::unload(mesh_t& mesh)
    {
        std::scoped_lock lock(renderer::res_system.deletion_mutex);

        if (mesh.vertex_buffer != VK_NULL_HANDLE)
        {
            renderer::res_system.deletion_queue.push_back({
                .type = renderer::resource_type_e::BUFFER,
                .handle = {.buffer = {mesh.vertex_buffer, mesh.vertex_allocation}},
                .bindless_id = mesh.vertex_bindless_id,
                .gpu_timeline_value = renderer::res_system.timeline_value,
            });
        }

        if (mesh.index_buffer != VK_NULL_HANDLE)
        {
            renderer::res_system.deletion_queue.push_back({
                .type = renderer::resource_type_e::BUFFER,
                .handle = {.buffer = {mesh.index_buffer, mesh.index_allocation}},
                .bindless_id = mesh.index_bindless_id,
                .gpu_timeline_value = renderer::res_system.timeline_value,
            });
        }
    }
} // namespace smol
