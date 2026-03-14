#include "mesh.h"

#include "smol/asset.h"
#include "smol/assets/shader.h"
#include "smol/log.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_resources.h"
#include "smol/rendering/renderer_types.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <tinygltf/tiny_gltf.h>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace smol
{
    std::optional<mesh_t> asset_loader_t<mesh_t>::load(const std::string& path)
    {
        tinygltf::TinyGLTF loader;
        tinygltf::Model model;
        std::string warn, err;

        bool is_glb = path.ends_with(".glb");
        bool loaded = is_glb ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
                             : loader.LoadASCIIFromFile(&model, &err, &warn, path);

        if (!loaded)
        {
            SMOL_LOG_ERROR("MESH", "GLTF load error: {}", err);
            return std::nullopt;
        }

        if (model.meshes.empty())
        {
            SMOL_LOG_ERROR("MESH", "GLTF file has no meshes: {}", path);
            return std::nullopt;
        }

        mesh_t mesh_asset;

        const tinygltf::Mesh& mesh = model.meshes[0];
        const tinygltf::Primitive& primitive = mesh.primitives[0];
        const std::map<std::string, int>& attribs = primitive.attributes;

        const tinygltf::Accessor& pos_accessor = model.accessors[primitive.attributes.at("POSITION")];
        const tinygltf::BufferView& pos_view = model.bufferViews[pos_accessor.bufferView];
        const tinygltf::Buffer& pos_buffer = model.buffers[pos_view.buffer];
        const f32* positions =
            reinterpret_cast<const f32*>(&pos_buffer.data[pos_view.byteOffset + pos_accessor.byteOffset]);

        mesh_asset.vertex_count = pos_accessor.count;

        const f32* normals = nullptr;
        const bool has_normals = attribs.find("NORMAL") != attribs.end();
        if (has_normals)
        {
            const tinygltf::Accessor& norm_accessor = model.accessors[attribs.at("NORMAL")];
            const tinygltf::BufferView& norm_view = model.bufferViews[norm_accessor.bufferView];
            const tinygltf::Buffer& norm_buffer = model.buffers[norm_view.buffer];
            normals = reinterpret_cast<const f32*>(&norm_buffer.data[norm_view.byteOffset + norm_accessor.byteOffset]);
        }

        const f32* uvs = nullptr;
        const bool has_uvs = attribs.find("TEXCOORD_0") != attribs.end();
        if (has_uvs)
        {
            const tinygltf::Accessor& uv_accessor = model.accessors[attribs.at("TEXCOORD_0")];
            const tinygltf::BufferView& uv_view = model.bufferViews[uv_accessor.bufferView];
            const tinygltf::Buffer& uv_buffer = model.buffers[uv_view.buffer];
            uvs = reinterpret_cast<const f32*>(&uv_buffer.data[uv_view.byteOffset + uv_accessor.byteOffset]);
        }

        std::vector<vertex_t> vertex_data(mesh_asset.vertex_count);
        for (i32 i = 0; i < mesh_asset.vertex_count; ++i)
        {
            vertex_t& v = vertex_data[i];
            std::memcpy(v.position, &positions[i * 3], 3 * sizeof(f32));
            if (has_normals) std::memcpy(v.normal, &normals[i * 3], 3 * sizeof(f32));
            else std::memset(v.normal, 0, 3 * sizeof(f32));

            if (has_uvs) std::memcpy(v.uv, &uvs[i * 2], 2 * sizeof(f32));
            else std::memset(v.uv, 0, 2 * sizeof(f32));
        }

        std::vector<u32> indices;
        if (primitive.indices >= 0)
        {
            const tinygltf::Accessor& idx_accessor = model.accessors[primitive.indices];
            const tinygltf::BufferView& idx_view = model.bufferViews[idx_accessor.bufferView];
            const tinygltf::Buffer& idx_buffer = model.buffers[idx_view.buffer];

            const void* raw_indices = &idx_buffer.data[idx_view.byteOffset + idx_accessor.byteOffset];
            indices.resize(idx_accessor.count);

            switch (idx_accessor.componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            {
                const u16* src = reinterpret_cast<const u16*>(raw_indices);
                for (size_t i = 0; i < indices.size(); ++i) indices[i] = static_cast<u32>(src[i]);
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            {
                const u32* src = reinterpret_cast<const u32*>(raw_indices);
                std::copy(src, src + idx_accessor.count, indices.begin());
                break;
            }
            default:
            {
                SMOL_LOG_ERROR("MESH", "Unsupported index type in asset: {}", path);
                return std::nullopt;
            }
            }

            mesh_asset.index_count = static_cast<i32>(indices.size());
            mesh_asset.uses_indices = true;
        }

        VkDeviceSize vertex_size = vertex_data.size() * sizeof(vertex_t);
        VkDeviceSize index_size = indices.size() * sizeof(u32);
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

        std::memcpy(staging_alloc_info.pMappedData, vertex_data.data(), static_cast<size_t>(vertex_size));
        if (index_size > 0)
        {
            std::memcpy((u8*)staging_alloc_info.pMappedData + vertex_size, indices.data(),
                        static_cast<size_t>(index_size));
        }

        VkBufferCreateInfo mesh_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = vertex_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        };
        VmaAllocationCreateInfo mesh_alloc_info = {
            .usage = VMA_MEMORY_USAGE_AUTO,
        };

        VK_CHECK(vmaCreateBuffer(renderer::ctx.allocator, &mesh_info, &mesh_alloc_info, &mesh_asset.vertex_buffer,
                                 &mesh_asset.vertex_allocation, nullptr));

        if (index_size > 0)
        {
            mesh_info.size = index_size;
            mesh_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            VK_CHECK(vmaCreateBuffer(renderer::ctx.allocator, &mesh_info, &mesh_alloc_info, &mesh_asset.index_buffer,
                                     &mesh_asset.index_allocation, nullptr));
        }

        VkCommandBuffer cmd_buf = renderer::begin_transfer_commands();

        VkBufferCopy copy_region = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = vertex_size,
        };
        vkCmdCopyBuffer(cmd_buf, staging_buf, mesh_asset.vertex_buffer, 1, &copy_region);

        if (index_size > 0)
        {
            copy_region.srcOffset = vertex_size;
            copy_region.size = index_size;
            vkCmdCopyBuffer(cmd_buf, staging_buf, mesh_asset.index_buffer, 1, &copy_region);
        }

        std::vector<VkBufferMemoryBarrier> barriers;
        VkBufferMemoryBarrier base_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = 0,
            .srcQueueFamilyIndex = renderer::ctx.queue_fam_indices.transfer_family.value(),
            .dstQueueFamilyIndex = renderer::ctx.queue_fam_indices.graphics_family.value(),
        };

        base_barrier.buffer = mesh_asset.vertex_buffer;
        base_barrier.size = vertex_size;
        barriers.push_back(base_barrier);

        if (index_size > 0)
        {
            base_barrier.buffer = mesh_asset.index_buffer;
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

        mesh_asset.vertex_bindless_id = renderer::res_system.buffer_heap.acquire();
        VkDescriptorBufferInfo vertex_info = {.buffer = mesh_asset.vertex_buffer, .offset = 0, .range = VK_WHOLE_SIZE};
        VkWriteDescriptorSet vertex_write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = renderer::res_system.global_set,
            .dstBinding = renderer::STORAGE_BUFFERS_BINDING_POINT,
            .dstArrayElement = mesh_asset.vertex_bindless_id,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &vertex_info,
        };
        vkUpdateDescriptorSets(renderer::ctx.device, 1, &vertex_write, 0, nullptr);

        if (index_size > 0)
        {
            mesh_asset.index_bindless_id = renderer::res_system.buffer_heap.acquire();
            VkDescriptorBufferInfo index_info = {
                .buffer = mesh_asset.index_buffer, .offset = 0, .range = VK_WHOLE_SIZE};
            VkWriteDescriptorSet index_write = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = renderer::res_system.global_set,
                .dstBinding = renderer::STORAGE_BUFFERS_BINDING_POINT,
                .dstArrayElement = mesh_asset.index_bindless_id,
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

        return mesh_asset;
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
