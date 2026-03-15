#include "texture.h"

#include "smol/asset.h"
#include "smol/defines.h"
#include "smol/log.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_resources.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/vulkan.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <stb/stb_image.h>
#include <vector>

namespace smol
{
    std::optional<texture_t> asset_loader_t<texture_t>::load(const std::string& path, texture_format_e type)
    {
        texture_t tex;
        tex.type = type;

        i32 channels;
        stbi_uc* pixels = stbi_load(path.c_str(), &tex.width, &tex.height, &channels, STBI_rgb_alpha);

        if (!pixels)
        {
            SMOL_LOG_ERROR("TEXTURE", "Failed to load texture: {}", path);
            return std::nullopt;
        }

        VkDeviceSize image_size = tex.width * tex.height * 4;

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
        if (vmaCreateBuffer(renderer::ctx.allocator, &staging_info, &alloc_info, &staging_buf, &staging_alloc,
                            &staging_alloc_info) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("TEXTURE", "Failed to allocate staging memory for texture: {}", path);
            return std::nullopt;
        }

        std::memcpy(staging_alloc_info.pMappedData, pixels, static_cast<size_t>(image_size));
        stbi_image_free(pixels);

        VkFormat format = (type == texture_format_e::SRGB) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

        VkImageCreateInfo image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = {static_cast<u32_t>(tex.width), static_cast<u32_t>(tex.height), 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        VmaAllocationCreateInfo img_alloc_info = {.usage = VMA_MEMORY_USAGE_AUTO};
        if (vmaCreateImage(renderer::ctx.allocator, &image_info, &img_alloc_info, &tex.image, &tex.allocation,
                           nullptr) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("TEXTURE", "Failed to allocate GPU memory for texture: {}", path);
            return std::nullopt;
        }

        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = tex.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
        };

        if (vkCreateImageView(renderer::ctx.device, &view_info, nullptr, &tex.view) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("TEXTURE", "Failed to create image view for texture: {}", path);
            return std::nullopt;
        }

        VkCommandBuffer cmd_buf = renderer::begin_transfer_commands();

        VkImageMemoryBarrier barrier_to_dst = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = tex.image,
            .subresourceRange = view_info.subresourceRange,
        };

        vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &barrier_to_dst);

        VkBufferImageCopy copy_region = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .mipLevel = 0,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {static_cast<u32_t>(tex.width), static_cast<u32_t>(tex.height), 1},
        };

        vkCmdCopyBufferToImage(cmd_buf, staging_buf, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

        VkImageMemoryBarrier release_barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = renderer::ctx.queue_fam_indices.transfer_family.value(),
            .dstQueueFamilyIndex = renderer::ctx.queue_fam_indices.graphics_family.value(),
            .image = tex.image,
            .subresourceRange = view_info.subresourceRange,
        };

        vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &release_barrier);

        u64_t signal_value = renderer::submit_transfer_commands(cmd_buf);

        {
            std::scoped_lock lock(renderer::res_system.pending_mutex);
            VkImageMemoryBarrier acquire_barrier = release_barrier;
            acquire_barrier.srcAccessMask = 0;
            acquire_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            renderer::res_system.pending_acquires.push_back({
                .type = renderer::resource_type_e::TEXTURE,
                .handle = {.image = tex.image},
                .barrier = {.image_barrier = acquire_barrier},
            });
        }

        tex.bindless_id = renderer::res_system.texture_heap.acquire();

        VkDescriptorImageInfo image_desc_info = {
            .sampler = VK_NULL_HANDLE,
            .imageView = tex.view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        VkWriteDescriptorSet write_desc = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = renderer::res_system.global_set,
            .dstBinding = renderer::TEXTURES_BINDING_POINT,
            .dstArrayElement = tex.bindless_id,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &image_desc_info,
        };

        vkUpdateDescriptorSets(renderer::ctx.device, 1, &write_desc, 0, nullptr);

        {
            std::scoped_lock lock(renderer::res_system.deletion_mutex);
            renderer::res_system.deletion_queue.push_back({
                .type = renderer::resource_type_e::BUFFER,
                .handle = {.buffer = {staging_buf, staging_alloc}},
                .bindless_id = renderer::BINDLESS_NULL_HANDLE,
                .gpu_timeline_value = signal_value,
            });
        }

        return tex;
    }

    void asset_loader_t<texture_t>::unload(texture_t& tex)
    {
        if (tex.image == VK_NULL_HANDLE) { return; }

        std::scoped_lock lock(renderer::res_system.deletion_mutex);

        renderer::res_system.deletion_queue.push_back({
            .type = renderer::resource_type_e::TEXTURE,
            .handle = {.texture = {tex.image, tex.allocation, tex.view}},
            .bindless_id = tex.bindless_id,
            .gpu_timeline_value = renderer::res_system.timeline_value,
        });
    }
} // namespace smol
