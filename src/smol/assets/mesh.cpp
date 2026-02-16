#include "mesh.h"

#include "smol/asset.h"
#include "smol/log.h"
#include "smol/rendering/renderer.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <tinygltf/tiny_gltf.h>
#include <vulkan/vulkan_core.h>

namespace smol
{
    mesh_data_t::~mesh_data_t()
    {
        // this one too, should actually check if gpu is using them before deleting them
        if (renderer::ctx::device != VK_NULL_HANDLE)
        {
            if (vertex_buffer) { vkDestroyBuffer(renderer::ctx::device, vertex_buffer, nullptr); }
            if (vertex_memory) { vkFreeMemory(renderer::ctx::device, vertex_memory, nullptr); }
            if (index_buffer) { vkDestroyBuffer(renderer::ctx::device, index_buffer, nullptr); }
            if (index_memory) { vkFreeMemory(renderer::ctx::device, index_memory, nullptr); }
        }
    }

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

        // not very efficient => 3 single vbos would be better
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
        VkDeviceMemory staging_mem;

        renderer::create_buffer(total_staging_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buf,
                                staging_mem);

        void* data;
        vkMapMemory(renderer::ctx::device, staging_mem, 0, total_staging_size, 0, &data);
        std::memcpy(data, vertex_data.data(), (size_t)vertex_size);
        if (index_size > 0) { std::memcpy((u8*)data + vertex_size, indices.data(), (size_t)index_size); }
        vkUnmapMemory(renderer::ctx::device, staging_mem);

        renderer::create_buffer(vertex_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh_asset.mesh_data->vertex_buffer,
                                mesh_asset.mesh_data->vertex_memory);

        if (index_size > 0)
        {
            renderer::create_buffer(index_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh_asset.mesh_data->index_buffer,
                                    mesh_asset.mesh_data->index_memory);
        }

        VkCommandBufferAllocateInfo cmd_alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmd_alloc.pNext = nullptr;
        cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_alloc.commandPool = renderer::ctx::command_pool;
        cmd_alloc.commandBufferCount = 1;

        VkCommandBuffer cmd_buf;
        {
            std::scoped_lock lock(renderer::ctx::queue_mutex);
            vkAllocateCommandBuffers(renderer::ctx::device, &cmd_alloc, &cmd_buf);
        }

        VkCommandBufferBeginInfo cmd_buf_begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        cmd_buf_begin_info.pNext = nullptr;
        cmd_buf_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_buf, &cmd_buf_begin_info);

        VkBufferCopy copy_region = {};
        copy_region.size = vertex_size;
        vkCmdCopyBuffer(cmd_buf, staging_buf, mesh_asset.mesh_data->vertex_buffer, 1, &copy_region);

        if (index_size > 0)
        {
            copy_region.srcOffset = vertex_size;
            copy_region.dstOffset = 0;
            copy_region.size = index_size;
            vkCmdCopyBuffer(cmd_buf, staging_buf, mesh_asset.mesh_data->index_buffer, 1, &copy_region);
        }

        VkBufferMemoryBarrier barriers[2] = {};
        barriers[0].pNext = nullptr;
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].buffer = mesh_asset.mesh_data->vertex_buffer;
        barriers[0].offset = 0;
        barriers[0].size = vertex_size;

        u32 barrier_count = 1;
        if (index_size > 0)
        {
            barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barriers[1].dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
            barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].buffer = mesh_asset.mesh_data->index_buffer;
            barriers[1].offset = 0;
            barriers[1].size = index_size;
            barrier_count++;
        }

        vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr,
                             barrier_count, barriers, 0, nullptr);

        vkEndCommandBuffer(cmd_buf);

        VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.pNext = nullptr;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd_buf;

        VkFence fence;
        VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_info.pNext = nullptr;
        vkCreateFence(renderer::ctx::device, &fence_info, nullptr, &fence);

        {
            std::scoped_lock lock(renderer::ctx::queue_mutex);
            vkQueueSubmit(renderer::ctx::graphics_queue, 1, &submit, fence);
        }

        vkWaitForFences(renderer::ctx::device, 1, &fence, VK_TRUE, UINT64_MAX);

        vkDestroyFence(renderer::ctx::device, fence, nullptr);

        {
            std::scoped_lock lock(renderer::ctx::queue_mutex);
            vkFreeCommandBuffers(renderer::ctx::device, renderer::ctx::command_pool, 1, &cmd_buf);
        }

        vkDestroyBuffer(renderer::ctx::device, staging_buf, nullptr);
        vkFreeMemory(renderer::ctx::device, staging_mem, nullptr);

        return mesh_asset;
    }
} // namespace smol
