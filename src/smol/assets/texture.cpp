#include "texture.h"

#include "smol/asset.h"
#include "smol/defines.h"
#include "smol/log.h"
#include "smol/main_thread.h"
#include "smol/rendering/renderer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <stb/stb_image.h>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace smol
{
    texture_data_t::~texture_data_t()
    {
        // this should probably be destroyed after i made sure that the gpu doesnt use the resource, but oh well :)
        if (renderer::ctx::device != VK_NULL_HANDLE)
        {
            if (sampler) { vkDestroySampler(renderer::ctx::device, sampler, nullptr); }
            if (view) { vkDestroyImageView(renderer::ctx::device, view, nullptr); }
            if (image) { vkDestroyImage(renderer::ctx::device, image, nullptr); }
            if (memory) { vkFreeMemory(renderer::ctx::device, memory, nullptr); }
        }
    }

    std::optional<texture_t> asset_loader_t<texture_t>::load(const std::string& path, texture_format_e type)
    {
        i32 width, height, channels;
        u8* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

        if (!pixels)
        {
            SMOL_LOG_ERROR("TEXTURE", "Failed to load image: {}", path);
            return std::nullopt;
        }

        VkDeviceSize image_size = width * height * 4;

        texture_t tex_asset;
        tex_asset.width = width;
        tex_asset.height = height;
        tex_asset.type = type;

        VkBuffer staging_buf;
        VkDeviceMemory staging_mem;

        renderer::create_buffer(image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buf,
                                staging_mem);

        void* data;
        vkMapMemory(renderer::ctx::device, staging_mem, 0, image_size, 0, &data);
        std::memcpy(data, pixels, static_cast<size_t>(image_size));
        vkUnmapMemory(renderer::ctx::device, staging_mem);

        stbi_image_free(pixels);

        VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image_info.pNext = nullptr;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent.width = static_cast<u32>(width);
        image_info.extent.height = static_cast<u32>(height);
        image_info.extent.depth = 1;
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        // the format is currently the only thing that parametrised
        image_info.format = (type == texture_format_e::SRGB) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(renderer::ctx::device, &image_info, nullptr, &tex_asset.tex_data->image) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("TEXTURE", "Failed to create Vulkan image with image at path: {}", path);
            vkDestroyBuffer(renderer::ctx::device, staging_buf, nullptr);
            vkFreeMemory(renderer::ctx::device, staging_mem, nullptr);
            return std::nullopt;
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(renderer::ctx::device, tex_asset.tex_data->image, &mem_reqs);

        VkMemoryAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc_info.pNext = nullptr;
        alloc_info.allocationSize = mem_reqs.size;
        alloc_info.memoryTypeIndex =
            renderer::find_mem_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(renderer::ctx::device, &alloc_info, nullptr, &tex_asset.tex_data->memory) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("TEXTURE", "Failed to allocate memory for texture: {}", path);
            return std::nullopt;
        }

        vkBindImageMemory(renderer::ctx::device, tex_asset.tex_data->image, tex_asset.tex_data->memory, 0);

        {
            VkCommandBufferAllocateInfo cmd_alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            cmd_alloc_info.pNext = nullptr;
            cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmd_alloc_info.commandPool = renderer::ctx::command_pool;
            cmd_alloc_info.commandBufferCount = 1;

            VkCommandBuffer cmd_buf;

            {
                std::scoped_lock lock(renderer::ctx::queue_mutex);
                vkAllocateCommandBuffers(renderer::ctx::device, &cmd_alloc_info, &cmd_buf);
            }

            VkCommandBufferBeginInfo cmd_buf_begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            cmd_buf_begin_info.pNext = nullptr;
            cmd_buf_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(cmd_buf, &cmd_buf_begin_info);

            VkImageMemoryBarrier mem_barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            mem_barrier.pNext = nullptr;
            mem_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            mem_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            mem_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mem_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mem_barrier.image = tex_asset.tex_data->image;
            mem_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            mem_barrier.subresourceRange.baseMipLevel = 0;
            mem_barrier.subresourceRange.levelCount = 1;
            mem_barrier.subresourceRange.baseArrayLayer = 0;
            mem_barrier.subresourceRange.layerCount = 1;
            mem_barrier.srcAccessMask = 0;
            mem_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &mem_barrier);

            VkBufferImageCopy region = {};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {(u32)width, (u32)height, 1};

            vkCmdCopyBufferToImage(cmd_buf, staging_buf, tex_asset.tex_data->image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            mem_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            mem_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            mem_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            mem_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &mem_barrier);

            vkEndCommandBuffer(cmd_buf);

            VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit_info.pNext = nullptr;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &cmd_buf;

            VkFence fence;
            VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fence_info.pNext = nullptr;
            vkCreateFence(renderer::ctx::device, &fence_info, nullptr, &fence);

            {
                std::scoped_lock lock(renderer::ctx::queue_mutex);
                vkQueueSubmit(renderer::ctx::graphics_queue, 1, &submit_info, fence);
            }

            vkWaitForFences(renderer::ctx::device, 1, &fence, VK_TRUE, UINT64_MAX);

            vkDestroyFence(renderer::ctx::device, fence, nullptr);
            vkFreeCommandBuffers(renderer::ctx::device, renderer::ctx::command_pool, 1, &cmd_buf);
        }

        vkDestroyBuffer(renderer::ctx::device, staging_buf, nullptr);
        vkFreeMemory(renderer::ctx::device, staging_mem, nullptr);

        VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.pNext = nullptr;
        view_info.image = tex_asset.tex_data->image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = image_info.format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        if (vkCreateImageView(renderer::ctx::device, &view_info, nullptr, &tex_asset.tex_data->view) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("TEXTURE", "Failed to create texture view: {}", path);
            return std::nullopt;
        }

        // this also needs to be parametrised
        VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.pNext = nullptr;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.anisotropyEnable = VK_FALSE;
        sampler_info.maxAnisotropy = 1.0f;
        sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        sampler_info.unnormalizedCoordinates = VK_FALSE;
        sampler_info.compareEnable = VK_FALSE;
        sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.mipLodBias = 0.0f;
        sampler_info.minLod = 0.0f;
        sampler_info.maxLod = 0.0f;

        if (vkCreateSampler(renderer::ctx::device, &sampler_info, nullptr, &tex_asset.tex_data->sampler) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("TEXTURE", "Failed to create texture sampler: {}", path);
            return std::nullopt;
        }

        return tex_asset;
    }
} // namespace smol