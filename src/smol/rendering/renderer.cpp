#include "renderer.h"

#include "smol/assets/material.h"
#include "smol/assets/mesh.h"
#include "smol/assets/shader.h"
#include "smol/components/camera.h"
#include "smol/components/renderer.h"
#include "smol/components/transform.h"
#include "smol/defines.h"
#include "smol/ecs_fwd.h"
#include "smol/log.h"
#include "smol/math.h"
#include "smol/rendering/renderer_resources.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/samplers.h"
#include "smol/rendering/vulkan.h"
#include "smol/systems/camera.h"
#include "smol/time.h"
#include "smol/window.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <Tracy/tracy/Tracy.hpp>
#include <algorithm>
#include <cglm/mat4.h>
#include <cglm/types.h>
#include <cglm/vec3.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <mutex>
#include <set>
#include <vector>

namespace smol::renderer
{
    render_context_t ctx;
    namespace { const std::vector<const char*> validation_layers = {"VK_LAYER_KHRONOS_validation"}; } // namespace

    namespace detail
    {
        bool check_validation_support()
        {
            u32_t layer_count;
            vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

            std::vector<VkLayerProperties> available_layers(layer_count);
            vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

            for (const char* layer_name : validation_layers)
            {
                bool layer_found = false;
                for (const VkLayerProperties& layer_props : available_layers)
                {
                    if (std::strcmp(layer_name, layer_props.layerName) == 0)
                    {
                        layer_found = true;
                        break;
                    }
                }

                if (!layer_found) { return false; }
            }

            return true;
        }

        static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
            VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
            [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT message_type,
            const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data, [[maybe_unused]] void* ptr_user_data)
        {
            if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            {
                SMOL_LOG_ERROR("VULKAN", "{}", p_callback_data->pMessage);
            }
            else if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            {
                SMOL_LOG_WARN("VULKAN", "{}", p_callback_data->pMessage);
            }
            else
            {
                SMOL_LOG_INFO("VULKAN", "{}", p_callback_data->pMessage);
            }

            return VK_FALSE;
        }

        queue_family_indices_t find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface)
        {
            queue_family_indices_t indices;
            u32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

            for (u32_t i = 0; i < count; i++)
            {
                if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    if (!indices.graphics_family.has_value()) { indices.graphics_family = i; }
                }

                if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { indices.compute_family = i; }

                if ((families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
                    !(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                {
                    if (!indices.transfer_family.has_value()) { indices.transfer_family = i; }
                }

                VkBool32 present_support = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
                if (present_support)
                {
                    if (i == indices.graphics_family) { indices.present_family = i; }
                    else if (!indices.present_family.has_value()) { indices.present_family = i; }
                }
            }

            if (!indices.transfer_family.has_value())
            {
                indices.transfer_family = indices.graphics_family;

                families[indices.graphics_family.value()].queueCount > 1 ? ctx.transfer_queue_index = 1
                                                                         : ctx.transfer_queue_index = 0;
            }

            return indices;
        }
    } // namespace detail

    bool init(const context_config_t& config, SDL_Window* window)
    {
        std::vector<const char*> instance_exts = config.required_instance_exts;

        VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        bool enable_validation = config.enable_validation && detail::check_validation_support();

        if (enable_validation)
        {
            instance_exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            debug_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug_create_info.pfnUserCallback = detail::debug_callback;

            for (const char* layer : validation_layers) { ctx.active_layers.push_back(layer); }
        }

        VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app_info.pApplicationName = config.app_name.c_str();
        app_info.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instance_info.pApplicationInfo = &app_info;
        instance_info.enabledExtensionCount = static_cast<u32_t>(instance_exts.size());
        instance_info.ppEnabledExtensionNames = instance_exts.data();
        instance_info.enabledLayerCount = enable_validation ? static_cast<u32_t>(validation_layers.size()) : 0;
        instance_info.ppEnabledLayerNames = enable_validation ? validation_layers.data() : nullptr;
        instance_info.pNext = enable_validation ? &debug_create_info : nullptr;

        VK_CHECK(vkCreateInstance(&instance_info, nullptr, &ctx.instance));

        volkLoadInstance(ctx.instance);

        for (const char* extension : instance_exts) { ctx.active_instance_exts.push_back(extension); }

        if (enable_validation)
        {
            PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                ctx.instance, "vkCreateDebugUtilsMessengerEXT");
            if (func) { func(ctx.instance, &debug_create_info, nullptr, &ctx.debug_messenger); }
        }

        if (!SDL_Vulkan_CreateSurface(window, ctx.instance, nullptr, &ctx.surface))
        {
            SMOL_LOG_FATAL("VULKAN", "Failed to create SDL surface: {}", SDL_GetError());
            return false;
        }

        u32_t device_count = 0;
        vkEnumeratePhysicalDevices(ctx.instance, &device_count, nullptr);
        std::vector<VkPhysicalDevice> devices(device_count);
        vkEnumeratePhysicalDevices(ctx.instance, &device_count, devices.data());

        for (const VkPhysicalDevice& device : devices)
        {
            queue_family_indices_t family_indices = detail::find_queue_families(device, ctx.surface);
            if (family_indices.is_complete())
            {
                ctx.physical_device = device;
                ctx.queue_fam_indices = family_indices;
                vkGetPhysicalDeviceProperties(device, &ctx.properties);
                break;
            }
        }

        if (ctx.physical_device == VK_NULL_HANDLE)
        {
            SMOL_LOG_FATAL("VULKAN", "No suitable GPU found");
            return false;
        }

        VkPhysicalDeviceFeatures2 device_features_check = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        VkPhysicalDeviceDescriptorIndexingFeatures supports_indexing = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
        device_features_check.pNext = &supports_indexing;

        vkGetPhysicalDeviceFeatures2(ctx.physical_device, &device_features_check);

        if (!supports_indexing.runtimeDescriptorArray)
        {
            SMOL_LOG_FATAL("VULKAN", "No GPU with bindless descriptor support found");
            return false;
        }

        std::vector<VkDeviceQueueCreateInfo> queue_infos;
        std::set<u32_t> unique_families = {ctx.queue_fam_indices.graphics_family.value(),
                                           ctx.queue_fam_indices.present_family.value(),
                                           ctx.queue_fam_indices.transfer_family.value()};

        float priority = 1.0f;
        for (u32_t family : unique_families)
        {
            VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queue_info.queueFamilyIndex = family;
            queue_info.queueCount = 1;
            queue_info.pQueuePriorities = &priority;
            queue_infos.push_back(queue_info);
        }

        // bindless descriptors
        VkPhysicalDeviceDescriptorIndexingFeatures indexing_features = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
        indexing_features.runtimeDescriptorArray = VK_TRUE;
        indexing_features.descriptorBindingPartiallyBound = VK_TRUE;
        indexing_features.descriptorBindingVariableDescriptorCount = VK_TRUE;

        indexing_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        indexing_features.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
        indexing_features.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;

        indexing_features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        indexing_features.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        indexing_features.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;

        indexing_features.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;

        VkPhysicalDeviceTimelineSemaphoreFeatures timeline_sem_features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
            .pNext = &indexing_features,
            .timelineSemaphore = VK_TRUE,
        };

        VkPhysicalDeviceVulkan11Features vk11_features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .pNext = &timeline_sem_features,
            .shaderDrawParameters = VK_TRUE,
        };

        VkPhysicalDeviceVulkan13Features vk13_features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = &vk11_features,
            .synchronization2 = VK_TRUE,
            .dynamicRendering = VK_TRUE,
        };

        VkPhysicalDeviceFeatures2 device_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        device_features_check.pNext = &vk13_features;
        device_features_check.features.samplerAnisotropy = VK_TRUE;
        device_features_check.features.multiDrawIndirect = VK_TRUE;

        std::vector<const char*> device_exts = config.required_device_exts;
        device_exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.pNext = &device_features_check;
        device_info.queueCreateInfoCount = static_cast<u32_t>(queue_infos.size());
        device_info.pQueueCreateInfos = queue_infos.data();
        device_info.enabledExtensionCount = static_cast<u32_t>(device_exts.size());
        device_info.ppEnabledExtensionNames = device_exts.data();

        VK_CHECK(vkCreateDevice(ctx.physical_device, &device_info, nullptr, &ctx.device));

        volkLoadDevice(ctx.device);

        for (const char* extension : device_exts) { ctx.active_device_exts.push_back(extension); }

        VmaAllocatorCreateInfo allocator_info = {};
        // allocator_info.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
        allocator_info.vulkanApiVersion = VK_API_VERSION_1_3;
        allocator_info.physicalDevice = ctx.physical_device;
        allocator_info.device = ctx.device;
        allocator_info.instance = ctx.instance;

        VmaVulkanFunctions vulkan_funcs = {};
        vmaImportVulkanFunctionsFromVolk(&allocator_info, &vulkan_funcs);

        allocator_info.pVulkanFunctions = &vulkan_funcs;

        VK_CHECK(vmaCreateAllocator(&allocator_info, &ctx.allocator));

        vkGetDeviceQueue(ctx.device, ctx.queue_fam_indices.graphics_family.value(), 0, &ctx.graphics_queue);
        vkGetDeviceQueue(ctx.device, ctx.queue_fam_indices.present_family.value(), 0, &ctx.present_queue);
        vkGetDeviceQueue(ctx.device, ctx.queue_fam_indices.transfer_family.value(), 0, &ctx.transfer_queue);

        init_resources();

        init_swapchain();

        VkCommandPoolCreateInfo transfer_pool_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = ctx.queue_fam_indices.transfer_family.value(),
        };

        VK_CHECK(vkCreateCommandPool(ctx.device, &transfer_pool_info, nullptr, &ctx.transfer_command_pool));

        VkSemaphoreTypeCreateInfo timeline_type_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0,
        };

        VkSemaphoreCreateInfo timeline_sem_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &timeline_type_info,
        };

        VK_CHECK(vkCreateSemaphore(ctx.device, &timeline_sem_info, nullptr, &ctx.timeline_semaphore));
        ctx.timeline_value = 0;

        VkSampler linear_repeat = create_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
        VkSampler linear_clamp = create_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        VkSampler nearest_repeat = create_sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
        VkSampler nearest_clamp = create_sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

        ctx.samplers.push_back(linear_repeat);
        ctx.samplers.push_back(linear_clamp);
        ctx.samplers.push_back(nearest_repeat);
        ctx.samplers.push_back(nearest_clamp);

        VkDescriptorImageInfo sampler_infos[4] = {};
        sampler_infos[(u32_t)sampler_type_e::LINEAR_REPEAT].sampler = linear_repeat;
        sampler_infos[(u32_t)sampler_type_e::LINEAR_CLAMP].sampler = linear_clamp;
        sampler_infos[(u32_t)sampler_type_e::NEAREST_REPEAT].sampler = nearest_repeat;
        sampler_infos[(u32_t)sampler_type_e::NEAREST_CLAMP].sampler = nearest_clamp;

        VkWriteDescriptorSet samplers_write_desc = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = res_system.global_set,
            .dstBinding = SAMPLERS_BINDING_POINT,
            .dstArrayElement = 0,
            .descriptorCount = 4,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .pImageInfo = sampler_infos,
        };

        vkUpdateDescriptorSets(ctx.device, 1, &samplers_write_desc, 0, nullptr);

        SMOL_LOG_INFO("VULKAN", "Context initialized for GPU: {}", ctx.properties.deviceName);
        return true;
    }

    void shutdown()
    {
        if (!ctx.samplers.empty())
        {
            for (VkSampler sampler : ctx.samplers) { vkDestroySampler(ctx.device, sampler, nullptr); }
        }

        res_system.process_deletions(UINT64_MAX);

        shutdown_resources();

        if (ctx.timeline_semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(ctx.device, ctx.timeline_semaphore, nullptr);
        }

        if (ctx.transfer_command_pool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(ctx.device, ctx.transfer_command_pool, nullptr);
        }

        for (per_frame_t& frame_data : ctx.per_frame_objects) { shutdown_per_frame(frame_data); }

        ctx.per_frame_objects.clear();

        for (VkSemaphore sem : ctx.recycled_semaphores) { vkDestroySemaphore(ctx.device, sem, nullptr); }

        for (VkImageView image_view : ctx.swapchain.views) { vkDestroyImageView(ctx.device, image_view, nullptr); }

        if (ctx.swapchain.depth_view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(ctx.device, ctx.swapchain.depth_view, nullptr);
        }

        if (ctx.swapchain.depth_image != VK_NULL_HANDLE)
        {
            vmaDestroyImage(ctx.allocator, ctx.swapchain.depth_image, ctx.swapchain.depth_allocation);
        }

        if (ctx.swapchain.handle != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(ctx.device, ctx.swapchain.handle, nullptr);
        }

        if (ctx.surface != VK_NULL_HANDLE) { vkDestroySurfaceKHR(ctx.instance, ctx.surface, nullptr); }

        if (ctx.allocator) { vmaDestroyAllocator(ctx.allocator); }

        if (ctx.device) { vkDestroyDevice(ctx.device, nullptr); }

        if (ctx.debug_messenger)
        {
            PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                ctx.instance, "vkDestroyDebugUtilsMessengerEXT");
            if (func) { func(ctx.instance, ctx.debug_messenger, nullptr); }
        }

        if (ctx.instance) { vkDestroyInstance(ctx.instance, nullptr); }
    }

    void render(ecs::registry_t& reg)
    {
        u64_t gpu_timeline_value = 0;
        vkGetSemaphoreCounterValue(ctx.device, res_system.timeline_semaphore, &gpu_timeline_value);

        res_system.process_deletions(gpu_timeline_value);

        u32_t index;
        VkResult res = acquire_next_image(&index);

        if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
        {
            resize(ctx.swapchain.extent.width, ctx.swapchain.extent.height);
            res = acquire_next_image(&index);
        }

        if (res != VK_SUCCESS) { vkQueueWaitIdle(ctx.present_queue); }

        // render here
        VkCommandBuffer cmd = ctx.per_frame_objects[index].main_command_buffer;

        VkCommandBufferBeginInfo cmd_begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };

        vkBeginCommandBuffer(cmd, &cmd_begin_info);

        {
            std::scoped_lock lock(res_system.pending_mutex);
            if (!res_system.pending_acquires.empty())
            {
                std::vector<VkImageMemoryBarrier> image_barriers;
                std::vector<VkBufferMemoryBarrier> buffer_barriers;

                for (pending_resource_t& res : res_system.pending_acquires)
                {
                    if (res.type == resource_type_e::TEXTURE) { image_barriers.push_back(res.barrier.image_barrier); }
                    else
                    {
                        buffer_barriers.push_back(res.barrier.buffer_barrier);
                    }
                }

                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0,
                                     nullptr, static_cast<u32_t>(buffer_barriers.size()), buffer_barriers.data(),
                                     static_cast<u32_t>(image_barriers.size()), image_barriers.data());

                res_system.pending_acquires.clear();
            }
        }

        VkClearValue clear_values[] = {
            {.color = {{0.01f, 0.01f, 0.01f, 1.0f}}},
            {.depthStencil = {1.0f, 0}},
        };

        per_frame_t& frame_data = ctx.per_frame_objects[index];

        ecs::entity_t active_cam = camera_system::get_active_camera(reg);
        if (active_cam != ecs::NULL_ENTITY)
        {
            camera_t& cam = reg.get<camera_t>(active_cam);
            transform_t& cam_transform = reg.get<transform_t>(active_cam);

            std::memcpy(frame_data.mapped_global_data->view.data, &cam.view, sizeof(mat4_t));
            std::memcpy(frame_data.mapped_global_data->projection.data, &cam.projection, sizeof(mat4_t));
            std::memcpy(frame_data.mapped_global_data->view_proj.data, &cam.view_proj, sizeof(mat4_t));

            frame_data.mapped_global_data->camera_pos.data.x = cam_transform.world_mat[3][0];
            frame_data.mapped_global_data->camera_pos.data.y = cam_transform.world_mat[3][1];
            frame_data.mapped_global_data->camera_pos.data.z = cam_transform.world_mat[3][2];
        }

        frame_data.mapped_global_data->time = time::time;

        transition_image(cmd, ctx.swapchain.images[index], VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        VkImageAspectFlags depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (ctx.swapchain.depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
            ctx.swapchain.depth_format == VK_FORMAT_D24_UNORM_S8_UINT)
        {
            depth_aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        transition_image(cmd, ctx.swapchain.depth_image, VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, depth_aspect);

        VkRenderingAttachmentInfo color_attachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = ctx.swapchain.views[index],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = clear_values[0],
        };

        VkRenderingAttachmentInfo depth_attachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = ctx.swapchain.depth_view,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = clear_values[1],
        };

        VkRenderingInfo rendering_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.extent = ctx.swapchain.extent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color_attachment,
            .pDepthAttachment = &depth_attachment,
        };

        vkCmdBeginRendering(cmd, &rendering_info);

        VkViewport vp = {
            .width = static_cast<f32>(ctx.swapchain.extent.width),
            .height = static_cast<f32>(ctx.swapchain.extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(cmd, 0, 1, &vp);

        VkRect2D scissor = {.extent = ctx.swapchain.extent};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        reg.sort<mesh_renderer_t>(
            [](const mesh_renderer_t& lhs, const mesh_renderer_t& rhs)
            {
                if (!lhs.material->shader || !rhs.material->shader)
                {
                    return lhs.material->shader > rhs.material->shader;
                }

                if (lhs.material->shader->pipeline != rhs.material->shader->pipeline)
                {
                    return lhs.material->shader->pipeline < rhs.material->shader->pipeline;
                }

                return lhs.material.get() < rhs.material.get();
            });

        std::vector<render_batch_t> batches;
        u32_t cur_object_id = 0;
        u32_t cur_material_offset = 0;
        u8* gpu_material_ptr = frame_data.mapped_material_data;

        material_t* last_processed_mat = nullptr;

        auto view = reg.view<transform_t, mesh_renderer_t>();

        for (auto [entity, transform, renderer] : view.each())
        {
            if (!renderer.active || !renderer.mesh || !renderer.material->shader) { continue; }

            material_t* cur_mat = renderer.material.get();

            if (cur_mat != last_processed_mat)
            {
                if (!cur_mat->data.empty())
                {
                    cur_mat->heap_offset = cur_material_offset;

                    std::memcpy(gpu_material_ptr + cur_material_offset, cur_mat->data.data(), cur_mat->data.size());

                    cur_material_offset += cur_mat->data.size();
                }

                last_processed_mat = cur_mat;
            }

            object_data_t& obj_data = frame_data.mapped_object_data[cur_object_id];
            std::memcpy(obj_data.model_matrix.data, &transform.world_mat, sizeof(mat4_t));
            obj_data.material_offset = cur_mat->heap_offset;
            obj_data.vertex_buffer_id = renderer.mesh->vertex_bindless_id;
            obj_data.index_buffer_id = renderer.mesh->index_bindless_id;

            VkDrawIndirectCommand& draw_cmd = frame_data.mapped_indirect_data[cur_object_id];
            draw_cmd.vertexCount =
                renderer.mesh->uses_indices ? renderer.mesh->index_count : renderer.mesh->vertex_count;
            draw_cmd.instanceCount = 1;
            draw_cmd.firstVertex = 0;
            draw_cmd.firstInstance = 0;

            VkPipeline cur_pipeline = renderer.material->shader->pipeline;
            VkPipelineLayout cur_layout = renderer.material->shader->pipeline_layout;

            if (batches.empty() || batches.back().pipeline != cur_pipeline)
            {
                batches.push_back({cur_pipeline, cur_layout, cur_object_id, 1});
            }
            else
            {
                batches.back().count++;
            }

            cur_object_id++;
        }

        struct
        {
            u32_t global_buffer_id;
            u32_t object_buffer_id;
            u32_t material_buffer_id;
        } pc_data = {
            frame_data.global_bindless_id,
            frame_data.object_bindless_id,
            frame_data.material_bindless_id,
        };

        for (const render_batch_t& batch : batches)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, batch.pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, batch.layout, 0, 1, &res_system.global_set, 0,
                                    nullptr);
            vkCmdPushConstants(cmd, batch.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(pc_data), &pc_data);

            vkCmdDrawIndirect(cmd, frame_data.indirect_buffer, batch.start_idx * sizeof(VkDrawIndirectCommand),
                              batch.count, sizeof(VkDrawIndirectCommand));
        }

        vkCmdEndRendering(cmd);

        transition_image(cmd, ctx.swapchain.images[index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        vkEndCommandBuffer(cmd);

        if (ctx.per_frame_objects[index].swapchain_release_semaphore == VK_NULL_HANDLE)
        {
            VkSemaphoreCreateInfo sem_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VK_CHECK(vkCreateSemaphore(ctx.device, &sem_info, nullptr,
                                       &ctx.per_frame_objects[index].swapchain_release_semaphore));
        }

        VkSemaphore wait_semaphores[] = {
            ctx.per_frame_objects[index].swapchain_acquire_semaphore,
            res_system.timeline_semaphore,
        };

        VkPipelineStageFlags wait_stages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        };

        u64_t wait_values[] = {0, res_system.timeline_value};

        u64_t next_timeline_value = ctx.timeline_value + 1;
        ctx.timeline_value = next_timeline_value;

        u64_t signal_values[] = {0, next_timeline_value};

        VkTimelineSemaphoreSubmitInfo timeline_sem_info = {
            .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
            .waitSemaphoreValueCount = 2,
            .pWaitSemaphoreValues = wait_values,
            .signalSemaphoreValueCount = 2,
            .pSignalSemaphoreValues = signal_values,
        };

        VkSemaphore signal_semaphores[] = {
            ctx.per_frame_objects[index].swapchain_release_semaphore,
            ctx.timeline_semaphore,
        };

        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = &timeline_sem_info,
            .waitSemaphoreCount = 2,
            .pWaitSemaphores = wait_semaphores,
            .pWaitDstStageMask = wait_stages,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
            .signalSemaphoreCount = 2,
            .pSignalSemaphores = signal_semaphores,
        };

        vkQueueSubmit(ctx.graphics_queue, 1, &submit_info, ctx.per_frame_objects[index].queue_submit_fence);

        res = present_image(index);

        if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
        {
            resize(ctx.swapchain.extent.width, ctx.swapchain.extent.height);
        }
    }

    VkFormat find_depth_format()
    {
        VkFormat formats[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
        for (VkFormat format : formats)
        {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(renderer::ctx.physical_device, format, &props);
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { return format; }
        }

        return VK_FORMAT_UNDEFINED;
    }

    VkSurfaceFormatKHR select_surface_format(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                                             std::vector<VkFormat> const& preferred_formats)
    {
        u32_t surface_format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &surface_format_count, nullptr);
        std::vector<VkSurfaceFormatKHR> supported_surface_formats(surface_format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &surface_format_count,
                                             supported_surface_formats.data());

        auto it =
            std::ranges::find_if(supported_surface_formats,
                                 [&preferred_formats](VkSurfaceFormatKHR surface_format)
                                 {
                                     return std::ranges::any_of(preferred_formats, [&surface_format](VkFormat format)
                                                                { return format == surface_format.format; });
                                 });

        return it != supported_surface_formats.end() ? *it : supported_surface_formats[0];
    }

    void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
                          VkImageAspectFlags aspect_mask)
    {
        VkImageMemoryBarrier2 barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .image = image,
            .subresourceRange = {.aspectMask = aspect_mask,
                                 .baseMipLevel = 0,
                                 .levelCount = VK_REMAINING_MIP_LEVELS,
                                 .baseArrayLayer = 0,
                                 .layerCount = VK_REMAINING_ARRAY_LAYERS},
        };

        if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            barrier.srcAccessMask = 0;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }
        else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
                 new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.srcStageMask =
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            barrier.srcAccessMask = 0;
            barrier.dstStageMask =
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            barrier.dstAccessMask =
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        }
        else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
                 new_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        {
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            barrier.dstAccessMask = 0;
        }
        else
        {
            SMOL_LOG_WARN("VULKAN", "transition_image: using fallback barrier for image transition");
        }

        VkDependencyInfo dep_info = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };

        vkCmdPipelineBarrier2(cmd, &dep_info);
    }

    bool resize(const u32_t width, const u32_t height)
    {
        VkSurfaceCapabilitiesKHR surface_caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physical_device, ctx.surface, &surface_caps);

        if (surface_caps.currentExtent.width == ctx.swapchain.extent.width &&
            surface_caps.currentExtent.height == ctx.swapchain.extent.height)
        {
            return false;
        }

        vkDeviceWaitIdle(ctx.device);

        init_swapchain();

        return true;
    }

    void init_swapchain()
    {
        VkSurfaceCapabilitiesKHR surface_caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physical_device, ctx.surface, &surface_caps);

        i32 w, h;
        smol::window::get_window_size(&w, &h);
        if (w == 0 || h == 0) return;

        VkExtent2D swapchain_extent;
        if (surface_caps.currentExtent.width != UINT32_MAX) { swapchain_extent = surface_caps.currentExtent; }
        else
        {
            swapchain_extent.width =
                std::clamp(static_cast<u32>(w), surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width);
            swapchain_extent.height =
                std::clamp(static_cast<u32>(h), surface_caps.minImageExtent.height, surface_caps.maxImageExtent.height);
        }
        ctx.swapchain.extent = swapchain_extent;
        VkSurfaceFormatKHR surface_format = select_surface_format(ctx.physical_device, ctx.surface);

        VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

        u32_t desired_swapchain_images = surface_caps.minImageCount + 1;
        if ((surface_caps.maxImageCount > 0) && (desired_swapchain_images > surface_caps.maxImageCount))
        {
            desired_swapchain_images = surface_caps.maxImageCount;
        }

        VkSurfaceTransformFlagBitsKHR pre_transform;
        if (surface_caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        {
            pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        }
        else
        {
            pre_transform = surface_caps.currentTransform;
        }

        VkSwapchainKHR old_swapchain = ctx.swapchain.handle;

        VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (surface_caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
        {
            composite = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        }
        else if (surface_caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
        {
            composite = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        }
        else if (surface_caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
        {
            composite = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        }

        VkSwapchainCreateInfoKHR swapchain_info = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = ctx.surface,
            .minImageCount = desired_swapchain_images,
            .imageFormat = surface_format.format,
            .imageColorSpace = surface_format.colorSpace,
            .imageExtent = swapchain_extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .preTransform = pre_transform,
            .compositeAlpha = composite,
            .presentMode = present_mode,
            .clipped = true,
            .oldSwapchain = old_swapchain,
        };

        VK_CHECK(vkCreateSwapchainKHR(ctx.device, &swapchain_info, nullptr, &ctx.swapchain.handle));

        if (old_swapchain != VK_NULL_HANDLE)
        {
            for (VkImageView view : ctx.swapchain.views) { vkDestroyImageView(ctx.device, view, nullptr); }

            for (per_frame_t& frame_data : ctx.per_frame_objects) { shutdown_per_frame(frame_data); }

            vkDestroyImageView(ctx.device, ctx.swapchain.depth_view, nullptr);
            vmaDestroyImage(ctx.allocator, ctx.swapchain.depth_image, ctx.swapchain.depth_allocation);

            ctx.swapchain.views.clear();
            vkDestroySwapchainKHR(ctx.device, old_swapchain, nullptr);
        }

        ctx.swapchain.extent = {swapchain_extent.width, swapchain_extent.height};
        ctx.swapchain.format = surface_format.format;

        ctx.swapchain.depth_format = find_depth_format();

        VkImageCreateInfo depth_image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = ctx.swapchain.depth_format,
            .extent = {swapchain_extent.width, swapchain_extent.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        VmaAllocationCreateInfo depth_alloc_info = {
            .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
            .priority = 1.0f,
        };

        VK_CHECK(vmaCreateImage(ctx.allocator, &depth_image_info, &depth_alloc_info, &ctx.swapchain.depth_image,
                                &ctx.swapchain.depth_allocation, nullptr));

        VkImageViewCreateInfo depth_view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = ctx.swapchain.depth_image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = ctx.swapchain.depth_format,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1}
        };

        if (ctx.swapchain.depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
            ctx.swapchain.depth_format == VK_FORMAT_D24_UNORM_S8_UINT)
        {
            depth_view_info.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        VK_CHECK(vkCreateImageView(ctx.device, &depth_view_info, nullptr, &ctx.swapchain.depth_view));

        u32_t image_count;
        VK_CHECK(vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain.handle, &image_count, nullptr));
        std::vector<VkImage> swapchain_images(image_count);
        VK_CHECK(vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain.handle, &image_count, swapchain_images.data()));
        ctx.swapchain.images = swapchain_images;

        ctx.per_frame_objects.clear();
        ctx.per_frame_objects.resize(image_count);

        for (size_t i = 0; i < image_count; i++) { init_per_frame(ctx.per_frame_objects[i]); }

        for (size_t i = 0; i < image_count; i++)
        {
            VkImageViewCreateInfo view_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = swapchain_images[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = ctx.swapchain.format,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                     .baseMipLevel = 0,
                                     .levelCount = 1,
                                     .baseArrayLayer = 0,
                                     .layerCount = 1},
            };

            VkImageView image_view;
            VK_CHECK(vkCreateImageView(ctx.device, &view_info, nullptr, &image_view));

            ctx.swapchain.views.push_back(image_view);
        }
    }

    VkResult acquire_next_image(u32_t* image)
    {
        VkSemaphore acquire_semaphore;
        if (ctx.recycled_semaphores.empty())
        {
            VkSemaphoreCreateInfo sem_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VK_CHECK(vkCreateSemaphore(ctx.device, &sem_info, nullptr, &acquire_semaphore));
        }
        else
        {
            acquire_semaphore = ctx.recycled_semaphores.back();
            ctx.recycled_semaphores.pop_back();
        }

        VkResult res = vkAcquireNextImageKHR(ctx.device, ctx.swapchain.handle, UINT64_MAX, acquire_semaphore,
                                             VK_NULL_HANDLE, image);

        if (res != VK_SUCCESS)
        {
            ctx.recycled_semaphores.push_back(acquire_semaphore);
            return res;
        }

        if (ctx.per_frame_objects[*image].queue_submit_fence != VK_NULL_HANDLE)
        {
            vkWaitForFences(ctx.device, 1, &ctx.per_frame_objects[*image].queue_submit_fence, true, UINT64_MAX);
            vkResetFences(ctx.device, 1, &ctx.per_frame_objects[*image].queue_submit_fence);
        }

        if (ctx.per_frame_objects[*image].main_command_pool != VK_NULL_HANDLE)
        {
            vkResetCommandPool(ctx.device, ctx.per_frame_objects[*image].main_command_pool, 0);
        }

        VkSemaphore old_semaphore = ctx.per_frame_objects[*image].swapchain_acquire_semaphore;

        if (old_semaphore != VK_NULL_HANDLE) { ctx.recycled_semaphores.push_back(old_semaphore); }

        ctx.per_frame_objects[*image].swapchain_acquire_semaphore = acquire_semaphore;

        return VK_SUCCESS;
    }

    VkResult present_image(u32_t image)
    {
        VkPresentInfoKHR present_info = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &ctx.per_frame_objects[image].swapchain_release_semaphore,
            .swapchainCount = 1,
            .pSwapchains = &ctx.swapchain.handle,
            .pImageIndices = &image,
        };

        return vkQueuePresentKHR(ctx.present_queue, &present_info);
    }

    void init_per_frame(per_frame_t& frame_data)
    {
        VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(ctx.device, &fence_info, nullptr, &frame_data.queue_submit_fence));

        VkCommandPoolCreateInfo cmd_pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        cmd_pool_info.queueFamilyIndex = static_cast<u32_t>(ctx.queue_fam_indices.graphics_family.value());
        VK_CHECK(vkCreateCommandPool(ctx.device, &cmd_pool_info, nullptr, &frame_data.main_command_pool));

        VkCommandBufferAllocateInfo cmd_buf_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmd_buf_info.commandPool = frame_data.main_command_pool;
        cmd_buf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_buf_info.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(ctx.device, &cmd_buf_info, &frame_data.main_command_buffer));

        auto create_mapped_buffer =
            [](VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VmaAllocation& alloc, void*& mapped_mem)
        {
            VkBufferCreateInfo buf_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = size,
                .usage = usage,
            };
            VmaAllocationCreateInfo alloc_info = {
                .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO,
            };

            VmaAllocationInfo vma_alloc_info;
            VK_CHECK(vmaCreateBuffer(ctx.allocator, &buf_info, &alloc_info, &buffer, &alloc, &vma_alloc_info));
            mapped_mem = vma_alloc_info.pMappedData;
        };

        auto make_bindless = [](VkBuffer buffer, u32_t& bindless_id)
        {
            bindless_id = res_system.buffer_heap.acquire();
            VkDescriptorBufferInfo desc_info = {
                .buffer = buffer,
                .offset = 0,
                .range = VK_WHOLE_SIZE,
            };
            VkWriteDescriptorSet write_desc = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = res_system.global_set,
                .dstBinding = STORAGE_BUFFERS_BINDING_POINT,
                .dstArrayElement = bindless_id,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &desc_info,
            };

            vkUpdateDescriptorSets(ctx.device, 1, &write_desc, 0, nullptr);
        };

        create_mapped_buffer(sizeof(global_data_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, frame_data.global_buffer,
                             frame_data.global_allocation, (void*&)frame_data.mapped_global_data);
        make_bindless(frame_data.global_buffer, frame_data.global_bindless_id);

        create_mapped_buffer(sizeof(object_data_t) * ecs::MAX_ENTITIES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             frame_data.object_buffer, frame_data.object_allocation,
                             (void*&)frame_data.mapped_object_data);
        make_bindless(frame_data.object_buffer, frame_data.object_bindless_id);

        create_mapped_buffer(sizeof(VkDrawIndirectCommand) * ecs::MAX_ENTITIES,
                             VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             frame_data.indirect_buffer, frame_data.indirect_allocation,
                             (void*&)frame_data.mapped_indirect_data);

        create_mapped_buffer(MAX_MATERIAL_BUFFER_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, frame_data.material_buffer,
                             frame_data.material_allocation, (void*&)frame_data.mapped_material_data);
        make_bindless(frame_data.material_buffer, frame_data.material_bindless_id);
    }

    void shutdown_per_frame(per_frame_t& frame_data)
    {
        if (frame_data.queue_submit_fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(ctx.device, frame_data.queue_submit_fence, nullptr);
            frame_data.queue_submit_fence = VK_NULL_HANDLE;
        }

        if (frame_data.main_command_buffer != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(ctx.device, frame_data.main_command_pool, 1, &frame_data.main_command_buffer);
            frame_data.main_command_buffer = VK_NULL_HANDLE;
        }

        if (frame_data.main_command_pool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(ctx.device, frame_data.main_command_pool, nullptr);
            frame_data.main_command_pool = VK_NULL_HANDLE;
        }

        if (frame_data.swapchain_acquire_semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(ctx.device, frame_data.swapchain_acquire_semaphore, nullptr);
            frame_data.swapchain_acquire_semaphore = VK_NULL_HANDLE;
        }

        if (frame_data.swapchain_release_semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(ctx.device, frame_data.swapchain_release_semaphore, nullptr);
            frame_data.swapchain_release_semaphore = VK_NULL_HANDLE;
        }

        if (frame_data.global_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(ctx.allocator, frame_data.global_buffer, frame_data.global_allocation);
            res_system.buffer_heap.release(frame_data.global_bindless_id);
        }

        if (frame_data.object_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(ctx.allocator, frame_data.object_buffer, frame_data.object_allocation);
            res_system.buffer_heap.release(frame_data.object_bindless_id);
        }

        if (frame_data.indirect_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(ctx.allocator, frame_data.indirect_buffer, frame_data.indirect_allocation);
        }

        if (frame_data.material_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(ctx.allocator, frame_data.material_buffer, frame_data.material_allocation);
        }
    }

    void create_buffer(VkDeviceSize size, VkBufferUsageFlags buffer_usage, VmaMemoryUsage mem_usage, VkBuffer& buffer,
                       VmaAllocation& allocation)
    {
        VkBufferCreateInfo buffer_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = buffer_usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE, // should be a parameter
        };

        VmaAllocationCreateInfo alloc_info = {.usage = mem_usage};

        VK_CHECK(vmaCreateBuffer(ctx.allocator, &buffer_info, &alloc_info, &buffer, &allocation, nullptr));
    }

    void create_image(u32 width, u32 height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                      VmaMemoryUsage mem_usage, VkImage& image, VmaAllocation& allocation)
    {
        VkImageCreateInfo image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = {width, height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = tiling,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        VmaAllocationCreateInfo alloc_info = {.usage = mem_usage};

        if (vmaCreateImage(ctx.allocator, &image_info, &alloc_info, &image, &allocation, nullptr) != VK_SUCCESS)
        {
            SMOL_LOG_ERROR("VULKAN", "Failed to create image; Width: {}, Height: {}", width, height);
        }
    }

    VkCommandBuffer begin_transfer_commands()
    {
        VkCommandBufferAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = ctx.transfer_command_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer cmd;
        {
            std::scoped_lock lock(ctx.transfer_mutex);
            VK_CHECK(vkAllocateCommandBuffers(ctx.device, &alloc_info, &cmd));
        }

        VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };

        VK_CHECK(vkBeginCommandBuffer(cmd, &begin_info));

        return cmd;
    }

    u64_t submit_transfer_commands(VkCommandBuffer cmd)
    {
        VK_CHECK(vkEndCommandBuffer(cmd));

        u64_t signal_value;
        {
            // if i add multithread asset loading, this should be locked
            signal_value = ++res_system.timeline_value;
        }

        VkTimelineSemaphoreSubmitInfo timeline_sem_info = {
            .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
            .signalSemaphoreValueCount = 1,
            .pSignalSemaphoreValues = &signal_value,
        };

        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = &timeline_sem_info,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &res_system.timeline_semaphore,
        };

        {
            std::scoped_lock lock(ctx.transfer_mutex);
            VK_CHECK(vkQueueSubmit(ctx.transfer_queue, 1, &submit_info, VK_NULL_HANDLE));
        }

        {
            std::scoped_lock lock(res_system.deletion_mutex);
            res_system.deletion_queue.push_back({
                .type = resource_type_e::COMMAND_BUFFER,
                .handle = {.cmd_buffer = {cmd, ctx.transfer_command_pool}},
                .bindless_id = BINDLESS_NULL_HANDLE,
                .gpu_timeline_value = signal_value,
            });
        }

        return signal_value;
    }

    VkSampler create_sampler(VkFilter filter, VkSamplerAddressMode address_mode)
    {
        VkSamplerCreateInfo sampler_info = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .flags = 0,
            .magFilter = filter,
            .minFilter = filter,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = address_mode,
            .addressModeV = address_mode,
            .addressModeW = address_mode,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_TRUE,
            .maxAnisotropy = ctx.properties.limits.maxSamplerAnisotropy,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0.0f,
            .maxLod = VK_LOD_CLAMP_NONE,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };

        VkSampler sampler = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSampler(ctx.device, &sampler_info, nullptr, &sampler));

        return sampler;
    }
} // namespace smol::renderer