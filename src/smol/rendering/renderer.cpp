#include "renderer.h"

#include "smol/asset.h"
#include "smol/assets/material.h"
#include "smol/assets/mesh.h"
#include "smol/assets/shader.h"
#include "smol/assets/texture.h"
#include "smol/components/camera.h"
#include "smol/components/lighting.h"
#include "smol/components/renderer.h"
#include "smol/components/transform.h"
#include "smol/defines.h"
#include "smol/ecs_fwd.h"
#include "smol/hash.h"
#include "smol/log.h"
#include "smol/math.h"
#include "smol/memory/linear_allocator.h"
#include "smol/profiling.h"
#include "smol/rendering/renderer_constants.h"
#include "smol/rendering/renderer_resources.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/rendergraph.h"
#include "smol/rendering/samplers.h"
#include "smol/rendering/vulkan.h"
#include "smol/systems/camera.h"
#include "smol/time.h"
#include "smol/window.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <cglm/cglm.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <set>
#ifdef SMOL_ENABLE_PROFILING
    #include <tracy/TracyVulkan.hpp>
#endif
#include <vector>

namespace smol::renderer
{
    render_context_t ctx;
    namespace
    {
        const std::vector<const char*> validation_layers = {"VK_LAYER_KHRONOS_validation"};
#ifdef SMOL_ENABLE_PROFILING
        TracyVkCtx tracy_vk_ctx = nullptr;
#endif

        rendergraph_t rendergraph;
        std::vector<graph_builder_func_t> custom_renderer_features;

        void recreate_surface()
        {
            vkDeviceWaitIdle(ctx.device);

            if (ctx.swapchain.handle != VK_NULL_HANDLE)
            {
                std::scoped_lock lock(res_system.deletion_mutex);
                u64_t safe_timeline = ctx.timeline_value;

                for (VkImageView view : ctx.swapchain.views)
                {
                    res_system.deletion_queue.push_back({
                        .type = resource_type_e::IMAGE_VIEW,
                        .handle = {.image_view = view},
                        .bindless_id = BINDLESS_NULL_HANDLE,
                        .gpu_timeline_value = safe_timeline,
                    });
                }
                ctx.swapchain.views.clear();

                res_system.deletion_queue.push_back({
                    .type = resource_type_e::TEXTURE,
                    .handle = {.texture = {ctx.swapchain.depth_image, ctx.swapchain.depth_allocation,
                                           ctx.swapchain.depth_view}},
                    .bindless_id = BINDLESS_NULL_HANDLE,
                    .gpu_timeline_value = safe_timeline,
                });

                vkDestroySwapchainKHR(ctx.device, ctx.swapchain.handle, nullptr);
                ctx.swapchain.handle = VK_NULL_HANDLE;
            }

            if (ctx.surface != VK_NULL_HANDLE)
            {
                vkDestroySurfaceKHR(ctx.instance, ctx.surface, nullptr);
                ctx.surface = VK_NULL_HANDLE;
            }

            if (!SDL_Vulkan_CreateSurface(smol::window::get_window(), ctx.instance, nullptr, &ctx.surface))
            {
                SMOL_LOG_WARN("VULKAN", "Failed to recreate Vulkan surface on resume");
            }
        }
    } // namespace

    void register_renderer_feature(graph_builder_func_t builder) { custom_renderer_features.push_back(builder); }

    SMOL_API rg_pass_t& add_mesh_pass(rendergraph_t& graph, u32_t name_hash, const char* debug_name,
                                      const std::string& target_pass_tag, const std::vector<rg_resource_id>& reads,
                                      const std::vector<rg_resource_id>& writes, rg_resource_id depth)
    {
        rg_pass_t& pass = graph.add_pass(name_hash, debug_name);
        pass.texture_reads = reads;
        pass.color_writes = writes;
        pass.depth_stencil = depth;

        pass.execute_callback = [target_pass_tag](VkCommandBuffer cmd, ecs::registry_t& reg)
        {
            per_frame_t& frame_data = ctx.per_frame_objects[ctx.cur_frame];

            if (frame_data.object_counter == 0 || frame_data.active_pipelines.empty()) return;

            push_constants_t pc_data = {
                frame_data.object_bindless_id,
                res_system.material_heap.bindless_id,
            };

            for (const active_pipeline_t& p : frame_data.active_pipelines)
            {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline);

                VkDescriptorSet sets[] = {res_system.global_set, res_system.frame_set};

                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 2, sets, 1,
                                        &frame_data.global_data_offset);
                vkCmdPushConstants(cmd, p.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(push_constants_t), &pc_data);

                u32_t indirect_offset = p.pipeline_index * frame_data.object_counter * 16;
                u32_t counts_offset = p.pipeline_index * sizeof(u32_t);

                vkCmdDrawIndirectCount(cmd, frame_data.indirect_buffer, indirect_offset, frame_data.draw_counts_buffer,
                                       counts_offset, frame_data.object_counter, 16);
            }
        };

        return pass;
    }

    SMOL_API rg_pass_t& add_fullscreen_pass(rendergraph_t& graph, u32_t name_hash, const char* debug_name,
                                            smol::material_t* material, const std::vector<rg_resource_id>& reads,
                                            const std::vector<rg_resource_id>& writes,
                                            std::function<void(rendergraph_t&, smol::material_t&)> on_execute)
    {
        rg_pass_t& pass = graph.add_pass(name_hash, debug_name);
        pass.texture_reads = reads;
        pass.color_writes = writes;

        pass.execute_callback = [&graph, material, on_execute](VkCommandBuffer cmd, ecs::registry_t& reg)
        {
            if (!material || !material->shader || !material->shader->ready()) { return; }

            if (on_execute) { on_execute(graph, *material); }

            material->sync();

            per_frame_t& frame_data = ctx.per_frame_objects[ctx.cur_frame];

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, material->shader->pipeline);

            VkDescriptorSet sets[] = {res_system.global_set, res_system.frame_set};

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, material->shader->pipeline_layout, 0, 2, sets,
                                    1, &frame_data.global_data_offset);

            push_constants_t pc_data = {
                frame_data.object_bindless_id,
                res_system.material_heap.bindless_id,
                material->heap_offset[ctx.cur_frame],
            };

            vkCmdPushConstants(cmd, material->shader->pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants_t),
                               &pc_data);

            vkCmdDraw(cmd, 3, 1, 0, 0);
        };

        return pass;
    }

    SMOL_API rg_pass_t& add_compute_pass(rendergraph_t& graph, u32_t name_hash, const char* debug_name,
                                         smol::material_t* material, u32_t dispatch_x, u32_t dispatch_y,
                                         u32_t dispatch_z, const std::vector<rg_resource_id>& reads,
                                         const std::vector<rg_resource_id>& writes,
                                         std::function<void(rendergraph_t&, smol::material_t&)> on_execute)
    {
        rg_pass_t& pass = graph.add_pass(name_hash, debug_name);
        pass.texture_reads = reads;
        pass.storage_writes = writes;

        pass.execute_callback = [&graph, material, dispatch_x, dispatch_y, dispatch_z, on_execute](VkCommandBuffer cmd,
                                                                                                   ecs::registry_t& reg)
        {
            if (!material || !material->shader || !material->shader->ready()) { return; }

            if (on_execute) { on_execute(graph, *material); }

            material->sync();

            per_frame_t& frame_data = ctx.per_frame_objects[ctx.cur_frame];

            VkDescriptorSet sets[] = {res_system.global_set, res_system.frame_set};

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, material->shader->pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, material->shader->pipeline_layout, 0, 2, sets,
                                    1, &frame_data.global_data_offset);

            push_constants_t pc_data = {
                frame_data.object_bindless_id,
                res_system.material_heap.bindless_id,
                material->heap_offset[ctx.cur_frame],
            };

            vkCmdPushConstants(cmd, material->shader->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               sizeof(push_constants_t), &pc_data);

            vkCmdDispatch(cmd, dispatch_x, dispatch_y, dispatch_z);
        };

        return pass;
    }

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

        VkValidationFeatureEnableEXT enable_validation_features[] = {
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
        };

        VkValidationFeaturesEXT validation_features = {
            .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
            .pNext = nullptr,
            .enabledValidationFeatureCount = 1,
            .pEnabledValidationFeatures = enable_validation_features,
        };

        if (enable_validation)
        {
            instance_exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            // instance_exts.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);

            debug_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug_create_info.pfnUserCallback = detail::debug_callback;

            for (const char* layer : validation_layers) { ctx.active_layers.push_back(layer); }

            // validation_features.pNext = &debug_create_info;
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
        std::set<u32_t> unique_families = {
            ctx.queue_fam_indices.graphics_family.value(),
            ctx.queue_fam_indices.present_family.value(),
            ctx.queue_fam_indices.transfer_family.value(),
        };

        float priority = 1.0f;
        for (u32_t family : unique_families)
        {
            VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queue_info.queueFamilyIndex = family;
            queue_info.queueCount = 1;
            queue_info.pQueuePriorities = &priority;
            queue_infos.push_back(queue_info);
        }

        VkPhysicalDeviceVulkan11Features vk11_features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .shaderDrawParameters = VK_TRUE,
        };

        VkPhysicalDeviceVulkan12Features vk12_features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = &vk11_features,

            .drawIndirectCount = VK_TRUE,

            .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
            .shaderStorageBufferArrayNonUniformIndexing = VK_TRUE,
            .shaderStorageImageArrayNonUniformIndexing = VK_TRUE,
            .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
            .descriptorBindingStorageImageUpdateAfterBind = VK_TRUE,
            .descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE,
            .descriptorBindingUpdateUnusedWhilePending = VK_TRUE,
            .descriptorBindingPartiallyBound = VK_TRUE,
            .descriptorBindingVariableDescriptorCount = VK_TRUE,

            .runtimeDescriptorArray = VK_TRUE,
            .timelineSemaphore = VK_TRUE,
        };

        VkPhysicalDeviceVulkan13Features vk13_features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = &vk12_features,
            .synchronization2 = VK_TRUE,
            .dynamicRendering = VK_TRUE,
        };

        VkPhysicalDeviceFeatures2 device_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        device_features_check.pNext = &vk13_features;
        device_features_check.features.samplerAnisotropy = VK_TRUE;
        device_features_check.features.multiDrawIndirect = VK_TRUE;
        device_features_check.features.drawIndirectFirstInstance = VK_TRUE;

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

        ctx.per_frame_objects.resize(MAX_FRAMES_IN_FLIGHT);
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) { init_per_frame(ctx.per_frame_objects[i]); }

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

#ifdef SMOL_ENABLE_PROFILING
        VkCommandPoolCreateInfo tracy_pool_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = ctx.queue_fam_indices.graphics_family.value(),
        };

        VkCommandPool tracy_pool;
        vkCreateCommandPool(ctx.device, &tracy_pool_info, nullptr, &tracy_pool);

        VkCommandBufferAllocateInfo tracy_alloc_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = tracy_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer tracy_cmd;
        vkAllocateCommandBuffers(ctx.device, &tracy_alloc_info, &tracy_cmd);

        tracy_vk_ctx = TracyVkContext(ctx.physical_device, ctx.device, ctx.graphics_queue, tracy_cmd);

        vkDestroyCommandPool(ctx.device, tracy_pool, nullptr);
#endif

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
                .requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            };

            VmaAllocationInfo vma_alloc_info;
            VK_CHECK(vmaCreateBuffer(ctx.allocator, &buf_info, &alloc_info, &buffer, &alloc, &vma_alloc_info));
            mapped_mem = vma_alloc_info.pMappedData;
        };

        VkDeviceSize min_alignment = ctx.properties.limits.minUniformBufferOffsetAlignment;
        ctx.global_data_aligned_size = (sizeof(global_data_t) + min_alignment - 1) & ~(min_alignment - 1);

        void* mapped_global_data;
        create_mapped_buffer(ctx.global_data_aligned_size * MAX_FRAMES_IN_FLIGHT, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             ctx.global_data_buffer, ctx.global_data_alloc, mapped_global_data);

        ctx.global_data_mapped = static_cast<u8*>(mapped_global_data);

        for (u32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            u32_t offset = i * ctx.global_data_aligned_size;
            ctx.per_frame_objects[i].mapped_global_data =
                reinterpret_cast<global_data_t*>(ctx.global_data_mapped + offset);
            ctx.per_frame_objects[i].global_data_offset = offset;
        }

        VkDescriptorBufferInfo ubo_info = {
            .buffer = ctx.global_data_buffer,
            .offset = 0,
            .range = sizeof(global_data_t),
        };

        VkWriteDescriptorSet write_desc = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = res_system.frame_set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .pBufferInfo = &ubo_info,
        };
        vkUpdateDescriptorSets(ctx.device, 1, &write_desc, 0, nullptr);

        ctx.culling_shader = smol::load_asset_sync<shader_t>("engine://assets/shaders/culling.slang");
        ctx.culling_instance.init(ctx.culling_shader);

        ctx.default_tex = smol::load_asset_sync<texture_t>("engine://assets/textures/default_white.png");

        return true;
    }

    void reset_assets()
    {
        ctx.culling_instance.shutdown();
        ctx.culling_shader.release();
        ctx.default_tex.release();
        rendergraph.clear();
        custom_renderer_features.clear();
    }

    void shutdown()
    {
#ifdef SMOL_ENABLE_PROFILING
        if (tracy_vk_ctx) { TracyVkDestroy(tracy_vk_ctx); }
#endif

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

        if (ctx.global_data_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(ctx.allocator, ctx.global_data_buffer, ctx.global_data_alloc);
        }

        for (VkImageView image_view : ctx.swapchain.views) { vkDestroyImageView(ctx.device, image_view, nullptr); }

        for (VkSemaphore sem : ctx.swapchain.release_semaphores) { vkDestroySemaphore(ctx.device, sem, nullptr); }

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
        ZoneScoped;

        bool needs_resize = false;
        u32_t new_width, new_height;
        for (auto [entity, event] : reg.view<window::window_size_changed_event>().each())
        {
            needs_resize = true;
            new_width = event.width;
            new_height = event.height;
        }

        if (needs_resize) { resize(new_width, new_height); }

        u64_t gpu_timeline_value = 0;
        vkGetSemaphoreCounterValue(ctx.device, res_system.timeline_semaphore, &gpu_timeline_value);

        res_system.process_deletions(gpu_timeline_value);

        u32_t index;
        VkResult res = acquire_next_image(&index);

        if (res == VK_ERROR_SURFACE_LOST_KHR)
        {
            recreate_surface();
            return;
        }
        else if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
        {
            resize(ctx.swapchain.extent.width, ctx.swapchain.extent.height);
            return;
        }
        else if (res != VK_SUCCESS)
        {
            vkQueueWaitIdle(ctx.present_queue);
            return;
        }

        // render here
        per_frame_t& frame_data = ctx.per_frame_objects[ctx.cur_frame];
        VkCommandBuffer cmd = frame_data.main_command_buffer;

        VkCommandBufferBeginInfo cmd_begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };

        vkBeginCommandBuffer(cmd, &cmd_begin_info);

#ifdef SMOL_ENABLE_PROFILING
        TracyVkCollect(tracy_vk_ctx, cmd);
#endif

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

        ecs::entity_t active_cam = camera_system::get_active_camera(reg);
        if (active_cam != ecs::NULL_ENTITY)
        {
            camera_t& cam = reg.get<camera_t>(active_cam);
            transform_t& cam_transform = reg.get<transform_t>(active_cam);

            std::memcpy(frame_data.mapped_global_data->view.data, &cam.view, sizeof(mat4_t));
            std::memcpy(frame_data.mapped_global_data->projection.data, &cam.projection, sizeof(mat4_t));

            mat4_t pre_rot_mat;
            if (ctx.cur_surface_transform & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR)
            {
                glm_rotate_z(pre_rot_mat, glm_rad(90.0f), pre_rot_mat);
            }
            else if (ctx.cur_surface_transform & VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR)
            {
                glm_rotate_z(pre_rot_mat, glm_rad(180.0f), pre_rot_mat);
            }
            else if (ctx.cur_surface_transform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR)
            {
                glm_rotate_z(pre_rot_mat, glm_rad(270.0f), pre_rot_mat);
            }

            mat4_t final_view_proj;
            glm_mat4_mul(pre_rot_mat, cam.view_proj, final_view_proj);
            std::memcpy(frame_data.mapped_global_data->view_proj.data, &final_view_proj, sizeof(mat4_t));

            vec4 planes[6];
            glm_frustum_planes(cam.view_proj, planes);
            std::memcpy(frame_data.mapped_global_data->frustum_planes, planes, sizeof(vec4) * 6);

            frame_data.mapped_global_data->camera_pos.data.x = cam_transform.world_mat[3][0];
            frame_data.mapped_global_data->camera_pos.data.y = cam_transform.world_mat[3][1];
            frame_data.mapped_global_data->camera_pos.data.z = cam_transform.world_mat[3][2];
        }

        frame_data.mapped_global_data->time = time::time;

        u32_t dir_count = 0;
        for (auto [entity, light, transform] : reg.view<directional_light_t, transform_t>().each())
        {
            if (dir_count >= MAX_DIR_LIGHTS) { break; }

            vec3_t dir = {transform.world_mat[2][0], transform.world_mat[2][1], transform.world_mat[2][2]};
            dir = vec3_t::normalize(dir);

            gpu_directional_light_t& gpu_light = frame_data.mapped_dir_lights[dir_count];

            gpu_light.direction_intensity.data.x = dir.x;
            gpu_light.direction_intensity.data.y = dir.y;
            gpu_light.direction_intensity.data.z = dir.z;
            gpu_light.direction_intensity.data.w = light.intensity;

            gpu_light.color.data.x = light.color.x;
            gpu_light.color.data.y = light.color.y;
            gpu_light.color.data.z = light.color.z;
            gpu_light.color.data.w = 0.0f;

            dir_count++;
        }

        u32_t point_count = 0;
        for (auto [entity, light, transform] : reg.view<point_light_t, transform_t>().each())
        {
            if (point_count >= MAX_LIGHTS) { break; }

            gpu_point_light_t& gpu_light = frame_data.mapped_point_lights[point_count];

            gpu_light.position_radius.data.x = transform.world_mat[3][0];
            gpu_light.position_radius.data.y = transform.world_mat[3][1];
            gpu_light.position_radius.data.z = transform.world_mat[3][2];
            gpu_light.position_radius.data.w = light.intensity;

            gpu_light.color_intensity.data.x = light.color.x;
            gpu_light.color_intensity.data.y = light.color.y;
            gpu_light.color_intensity.data.z = light.color.z;
            gpu_light.color_intensity.data.w = light.intensity;

            point_count++;
        }

        u32_t spot_count = 0;
        for (auto [entity, light, transform] : reg.view<spot_light_t, transform_t>().each())
        {
            if (spot_count >= MAX_LIGHTS) { break; }

            vec3_t dir = {transform.world_mat[2][0], transform.world_mat[2][1], transform.world_mat[2][2]};
            dir = vec3_t::normalize(dir);

            f32 inner_cos = std::cos(glm_rad(light.inner_angle));
            f32 outer_cos = std::cos(glm_rad(light.outer_angle));

            gpu_spot_light_t& gpu_light = frame_data.mapped_spot_lights[spot_count];

            gpu_light.position_range.data.x = transform.world_mat[3][0];
            gpu_light.position_range.data.y = transform.world_mat[3][1];
            gpu_light.position_range.data.z = transform.world_mat[3][2];
            gpu_light.position_range.data.w = light.radius;

            gpu_light.direction_intensity.data.x = dir.x;
            gpu_light.direction_intensity.data.y = dir.y;
            gpu_light.direction_intensity.data.z = dir.z;
            gpu_light.direction_intensity.data.w = light.intensity;

            gpu_light.color_inner_cos.data.x = light.color.x;
            gpu_light.color_inner_cos.data.y = light.color.y;
            gpu_light.color_inner_cos.data.z = light.color.z;
            gpu_light.color_inner_cos.data.w = inner_cos;

            gpu_light.outer_cos.data.x = outer_cos;
            gpu_light.outer_cos.data.y = 0.0f;
            gpu_light.outer_cos.data.z = 0.0f;
            gpu_light.outer_cos.data.w = 0.0f;

            spot_count++;
        }

        frame_data.mapped_global_data->dir_light_count = dir_count;
        frame_data.mapped_global_data->dir_light_buffer_id = frame_data.dir_light_bindless_id;

        frame_data.mapped_global_data->point_light_count = point_count;
        frame_data.mapped_global_data->point_light_buffer_id = frame_data.point_light_bindless_id;

        frame_data.mapped_global_data->spot_light_count = spot_count;
        frame_data.mapped_global_data->spot_light_buffer_id = frame_data.spot_light_bindless_id;

        auto get_blend_key = [](const std::string& mode) -> u32_t
        {
            if (mode == "Opaque") return 0;
            else if (mode == "Cutout") return 1;
            else if (mode == "TransparentAlpha") return 2;
            else if (mode == "TransparentAdd") return 3;
            else if (mode == "TransparentMult") return 4;
            return 5;
        };

        reg.sort<mesh_renderer_t>(
            [&](const mesh_renderer_t& lhs, const mesh_renderer_t& rhs)
            {
                if (!lhs.material->shader || !rhs.material->shader)
                {
                    return lhs.material->shader > rhs.material->shader;
                }

                u32_t l_key = get_blend_key(lhs.material->shader->module.blend_mode);
                u32_t r_key = get_blend_key(rhs.material->shader->module.blend_mode);

                if (l_key != r_key) { return l_key < r_key; }

                if (lhs.material->shader->pipeline != rhs.material->shader->pipeline)
                {
                    return lhs.material->shader->pipeline < rhs.material->shader->pipeline;
                }

                return lhs.material.get() < rhs.material.get();
            });

        u32_t cur_object_id = 0;
        frame_data.active_pipelines.clear();
        VkPipeline last_pipeline = VK_NULL_HANDLE;

        auto view = reg.view<transform_t, mesh_renderer_t>();
        for (auto [entity, transform, renderer] : view.each())
        {
            if (!renderer.active || !renderer.mesh || !renderer.material || !renderer.material->shader) { continue; }

            renderer.material->sync();

            if (renderer.material->shader->pipeline != last_pipeline)
            {
                last_pipeline = renderer.material->shader->pipeline;
                frame_data.active_pipelines.push_back({
                    last_pipeline,
                    renderer.material->shader->pipeline_layout,
                    static_cast<u32_t>(frame_data.active_pipelines.size()),
                    get_blend_key(renderer.material->shader->module.blend_mode),
                });
            }

            object_data_t& obj_data = frame_data.mapped_object_data[cur_object_id];
            std::memcpy(obj_data.model_matrix.data, &transform.world_mat, sizeof(mat4_t));

            mat4_t normal_mat;
            glm_mat4_inv(transform.world_mat, normal_mat);
            glm_mat4_transpose(normal_mat);
            std::memcpy(obj_data.normal_matrix.data, &normal_mat, sizeof(mat4_t));

            obj_data.material_offset = renderer.material->heap_offset[ctx.cur_frame];
            obj_data.vertex_buffer_id = renderer.mesh->vertex_bindless_id;
            obj_data.index_buffer_id = renderer.mesh->index_bindless_id;

            obj_data.vertex_count = renderer.mesh->vertex_count;
            obj_data.index_count = renderer.mesh->index_count;

            obj_data.pipeline_index = frame_data.active_pipelines.back().pipeline_index;

            vec3_t world_center;
            vec3_t local_c = renderer.mesh->local_center;
            glm_mat4_mulv3(transform.world_mat, local_c, 1.0f, world_center);

            obj_data.bounding_sphere_center.data.x = world_center.x;
            obj_data.bounding_sphere_center.data.y = world_center.y;
            obj_data.bounding_sphere_center.data.z = world_center.z;
            obj_data.bounding_sphere_center.data.w = 0.0f;

            f32 scale_x =
                glm_vec3_norm((vec3){transform.world_mat[0][0], transform.world_mat[0][1], transform.world_mat[0][2]});
            f32 scale_y =
                glm_vec3_norm((vec3){transform.world_mat[1][0], transform.world_mat[1][1], transform.world_mat[1][2]});
            f32 scale_z =
                glm_vec3_norm((vec3){transform.world_mat[2][0], transform.world_mat[2][1], transform.world_mat[2][2]});

            f32 max_scale = std::max({scale_x, scale_y, scale_z});
            obj_data.bounding_sphere_radius = renderer.mesh->local_radius * max_scale;

            cur_object_id++;
        }

        frame_data.object_counter = cur_object_id;
        frame_data.mapped_global_data->object_count = cur_object_id;
        frame_data.mapped_global_data->active_pipeline_count = frame_data.active_pipelines.size();

        if (cur_object_id > 0 && !frame_data.active_pipelines.empty())
        {
            VkDeviceSize req_counts = frame_data.active_pipelines.size() * sizeof(u32_t);
            VkDeviceSize req_indirect = frame_data.active_pipelines.size() * cur_object_id * 16;

            auto create_device_buffer =
                [](VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VmaAllocation& alloc)
            {
                VkBufferCreateInfo buf_info = {
                    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                    .size = size,
                    .usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                };
                VmaAllocationCreateInfo alloc_info = {
                    .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                };

                VK_CHECK(vmaCreateBuffer(ctx.allocator, &buf_info, &alloc_info, &buffer, &alloc, nullptr));
            };

            if (frame_data.draw_counts_size < req_counts)
            {
                if (frame_data.draw_counts_buffer != VK_NULL_HANDLE)
                {
                    vmaDestroyBuffer(ctx.allocator, frame_data.draw_counts_buffer, frame_data.draw_counts_alloc);
                }

                create_device_buffer(req_counts,
                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                     frame_data.draw_counts_buffer, frame_data.draw_counts_alloc);
                frame_data.draw_counts_size = req_counts;
            }

            if (frame_data.indirect_size < req_indirect)
            {
                if (frame_data.indirect_buffer != VK_NULL_HANDLE)
                {
                    vmaDestroyBuffer(ctx.allocator, frame_data.indirect_buffer, frame_data.indirect_alloc);
                }

                create_device_buffer(req_indirect,
                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                     frame_data.indirect_buffer, frame_data.indirect_alloc);
                frame_data.indirect_size = req_indirect;
            }

            vkCmdFillBuffer(cmd, frame_data.draw_counts_buffer, 0, req_counts, 0);

            VkBufferMemoryBarrier2 fill_barrier = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .buffer = frame_data.draw_counts_buffer,
                .offset = 0,
                .size = req_counts,
            };

            VkDependencyInfo fill_dep_info = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarriers = &fill_barrier,
            };

            vkCmdPipelineBarrier2(cmd, &fill_dep_info);

            ctx.culling_instance.set_buffer("draw_counts"_h, frame_data.draw_counts_buffer);
            ctx.culling_instance.set_buffer("indirect_commands"_h, frame_data.indirect_buffer);
            ctx.culling_instance.sync();

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx.culling_shader->pipeline);

            VkDescriptorSet sets[] = {res_system.global_set, res_system.frame_set};

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx.culling_shader->pipeline_layout, 0, 2,
                                    sets, 1, &frame_data.global_data_offset);

            ctx.culling_instance.bind(cmd);

            push_constants_t pc_data = {
                frame_data.object_bindless_id,
                res_system.material_heap.bindless_id,
                0,
            };
            vkCmdPushConstants(cmd, ctx.culling_shader->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               sizeof(push_constants_t), &pc_data);

            vkCmdDispatch(cmd, (cur_object_id + 63) / 64, 1, 1);

            VkBufferMemoryBarrier2 indirect_barriers[2] = {};
            indirect_barriers[0] = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                .buffer = frame_data.indirect_buffer,
                .offset = 0,
                .size = req_indirect,
            };

            indirect_barriers[1] = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                .buffer = frame_data.draw_counts_buffer,
                .offset = 0,
                .size = req_counts,
            };

            VkDependencyInfo indirect_dep_info = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .bufferMemoryBarrierCount = 2,
                .pBufferMemoryBarriers = indirect_barriers,
            };

            vkCmdPipelineBarrier2(cmd, &indirect_dep_info);
        }

        smol::active_arena = &frame_data.frame_allocator;

        rendergraph.clear();

        rg_resource_id swapchain_res = rendergraph.import_image(
            "Swapchain"_h, "Swapchain", ctx.swapchain.images[index], ctx.swapchain.views[index], ctx.swapchain.format,
            ctx.swapchain.extent.width, ctx.swapchain.extent.height);

        if (!ctx.use_offscreen_viewport) { ctx.render_extent = ctx.swapchain.extent; }

        image_desc_t color_desc = {
            .width = ctx.render_extent.width,
            .height = ctx.render_extent.height,
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
        };

        rg_resource_id color_res = rendergraph.create_image("SceneColor"_h, "SceneColor", color_desc);

        image_desc_t depth_desc = {
            .width = ctx.render_extent.width,
            .height = ctx.render_extent.height,
            .format = ctx.swapchain.depth_format,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
        };

        rg_resource_id depth_res = rendergraph.create_image("SceneDepth"_h, "SceneDepth", depth_desc);

        if (ctx.use_offscreen_viewport)
        {
            image_desc_t viewport_desc = {
                .width = ctx.render_extent.width,
                .height = ctx.render_extent.height,
                .format = ctx.swapchain.format,
                .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
            };

            rendergraph.create_image("ViewportColor"_h, "ViewportColor", viewport_desc);
            rendergraph.add_alias("FinalOutput"_h, "ViewportColor"_h);
        }
        else
        {
            rendergraph.add_alias("FinalOutput"_h, "Swapchain"_h);
        }

        for (graph_builder_func_t& feature_builder : custom_renderer_features) { feature_builder(rendergraph, reg); }

        rendergraph.compile(frame_data);

        if (ctx.use_offscreen_viewport)
        {
            ctx.viewport_bindless_id = rendergraph.get_bindless_id(rendergraph.get_resource("ViewportColor"_h));
        }

        rendergraph.execute(cmd, reg);

        VkImageLayout final_layout = rendergraph.get_layout(swapchain_res);
        if (final_layout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        {
            transition_image(cmd, ctx.swapchain.images[index], final_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                             VK_IMAGE_ASPECT_COLOR_BIT);
        }

        vkEndCommandBuffer(cmd);

        smol::active_arena = nullptr;

        VkSemaphore wait_semaphores[] = {
            frame_data.swapchain_acquire_semaphore,
            res_system.timeline_semaphore,
        };

        VkSemaphore signal_semaphores[] = {
            ctx.swapchain.release_semaphores[index],
            ctx.timeline_semaphore,
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

        vkQueueSubmit(ctx.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);

        frame_data.target_timeline_value = next_timeline_value;

        VkPresentInfoKHR present_info = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &ctx.swapchain.release_semaphores[index],
            .swapchainCount = 1,
            .pSwapchains = &ctx.swapchain.handle,
            .pImageIndices = &index,
        };

        res = vkQueuePresentKHR(ctx.present_queue, &present_info);

        if (res == VK_ERROR_SURFACE_LOST_KHR) { recreate_surface(); }
        else if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
        {
            resize(ctx.swapchain.extent.width, ctx.swapchain.extent.height);
        }

        ctx.cur_frame = (ctx.cur_frame + 1) % MAX_FRAMES_IN_FLIGHT;
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
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
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
        else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
                 new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        }
        else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        {
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.srcAccessMask = 0;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            barrier.dstAccessMask = 0;
        }
        else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                 new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }
        else if (old_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR &&
                 new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            barrier.srcAccessMask = 0;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }
        else
        {
            SMOL_LOG_WARN("VULKAN", "transition_image: using fallback barrier for image transition. From: {}, To: {}",
                          (u32_t)old_layout, (u32_t)new_layout);
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
        if (ctx.surface == VK_NULL_HANDLE) { return false; }

        VkSurfaceCapabilitiesKHR surface_caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physical_device, ctx.surface, &surface_caps);

        if (surface_caps.currentExtent.width == 0 && surface_caps.currentExtent.height == 0) { return false; }

        init_swapchain();

        return true;
    }

    void init_swapchain()
    {
        if (ctx.surface == VK_NULL_HANDLE) { return; }

        VkSurfaceCapabilitiesKHR surface_caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physical_device, ctx.surface, &surface_caps);

        if (surface_caps.currentExtent.width == 0 || surface_caps.currentExtent.height == 0) { return; }

        VkExtent2D logical_extent;
        if (surface_caps.currentExtent.width != UINT32_MAX) { logical_extent = surface_caps.currentExtent; }
        else
        {
            i32 w, h;
            smol::window::get_window_size(&w, &h);

            logical_extent.width =
                std::clamp(static_cast<u32_t>(w), surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width);
            logical_extent.height = std::clamp(static_cast<u32_t>(h), surface_caps.minImageExtent.height,
                                               surface_caps.maxImageExtent.height);
        }

        VkSurfaceTransformFlagBitsKHR pre_transform = surface_caps.currentTransform;
        VkExtent2D physical_extent = logical_extent;

        if (pre_transform & (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR))
        {
            physical_extent.width = logical_extent.height;
            physical_extent.height = logical_extent.width;
        }

        ctx.cur_surface_transform = pre_transform;
        ctx.swapchain.extent = physical_extent;

        if (!ctx.use_offscreen_viewport)
        {
            ctx.render_extent = physical_extent;
            ctx.logical_extent = logical_extent;
        }

        VkSurfaceFormatKHR surface_format = select_surface_format(ctx.physical_device, ctx.surface);
        VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

        u32_t desired_swapchain_images = surface_caps.minImageCount + 1;
        if ((surface_caps.maxImageCount > 0) && (desired_swapchain_images > surface_caps.maxImageCount))
        {
            desired_swapchain_images = surface_caps.maxImageCount;
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
            .imageExtent = physical_extent,
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
            std::scoped_lock lock(res_system.deletion_mutex);

            u64_t safe_timeline = ctx.timeline_value;

            for (VkImageView view : ctx.swapchain.views)
            {
                res_system.deletion_queue.push_back({
                    .type = resource_type_e::IMAGE_VIEW,
                    .handle = {.image_view = view},
                    .bindless_id = BINDLESS_NULL_HANDLE,
                    .gpu_timeline_value = safe_timeline,
                });
            }

            ctx.swapchain.views.clear();

            res_system.deletion_queue.push_back({
                .type = resource_type_e::TEXTURE,
                .handle = {.texture = {ctx.swapchain.depth_image, ctx.swapchain.depth_allocation,
                                       ctx.swapchain.depth_view}},
                .bindless_id = BINDLESS_NULL_HANDLE,
                .gpu_timeline_value = safe_timeline,
            });

            res_system.deletion_queue.push_back({
                .type = resource_type_e::SWAPCHAIN,
                .handle = {.swapchain = old_swapchain},
                .bindless_id = BINDLESS_NULL_HANDLE,
                .gpu_timeline_value = safe_timeline,
            });
        }

        ctx.swapchain.extent = {physical_extent.width, physical_extent.height};
        ctx.swapchain.format = surface_format.format;

        ctx.swapchain.depth_format = find_depth_format();

        VkImageCreateInfo depth_image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = ctx.swapchain.depth_format,
            .extent = {physical_extent.width, physical_extent.height, 1},
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

        if (!ctx.swapchain.release_semaphores.empty())
        {
            std::scoped_lock lock(res_system.deletion_mutex);

            u64_t safe_timeline = ctx.timeline_value;

            for (VkSemaphore sem : ctx.swapchain.release_semaphores)
            {
                res_system.deletion_queue.push_back({
                    .type = resource_type_e::SEMAPHORE,
                    .handle = {.semaphore = sem},
                    .bindless_id = BINDLESS_NULL_HANDLE,
                    .gpu_timeline_value = safe_timeline,
                });
            }

            ctx.swapchain.release_semaphores.clear();
        }

        ctx.swapchain.release_semaphores.clear();
        ctx.swapchain.release_semaphores.resize(image_count);

        for (size_t i = 0; i < image_count; i++)
        {
            VkSemaphoreCreateInfo sem_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VK_CHECK(vkCreateSemaphore(ctx.device, &sem_info, nullptr, &ctx.swapchain.release_semaphores[i]));
        }

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

    VkResult acquire_next_image(u32_t* image_index)
    {
        if (ctx.swapchain.handle == VK_NULL_HANDLE) { return VK_ERROR_OUT_OF_DATE_KHR; }

        per_frame_t& frame_data = ctx.per_frame_objects[ctx.cur_frame];

        if (frame_data.target_timeline_value > 0)
        {
            VkSemaphoreWaitInfo wait_info = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                .semaphoreCount = 1,
                .pSemaphores = &ctx.timeline_semaphore,
                .pValues = &frame_data.target_timeline_value,
            };

            vkWaitSemaphores(ctx.device, &wait_info, UINT64_MAX);
        }

        VkResult res = vkAcquireNextImageKHR(ctx.device, ctx.swapchain.handle, UINT64_MAX,
                                             frame_data.swapchain_acquire_semaphore, VK_NULL_HANDLE, image_index);

        if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_SURFACE_LOST_KHR)
        {
            return res;
        }

        vkResetCommandPool(ctx.device, frame_data.main_command_pool, 0);

        frame_data.transient_pool.reset();
        frame_data.frame_allocator.reset();

        return VK_SUCCESS;
    }

    void init_per_frame(per_frame_t& frame_data)
    {
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
                .requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
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

        auto create_device_buffer =
            [](VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VmaAllocation& alloc)
        {
            VkBufferCreateInfo buf_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = size,
                .usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            };
            VmaAllocationCreateInfo alloc_info = {
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            };

            VK_CHECK(vmaCreateBuffer(ctx.allocator, &buf_info, &alloc_info, &buffer, &alloc, nullptr));
        };

        create_mapped_buffer(sizeof(object_data_t) * ecs::MAX_ENTITIES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             frame_data.object_buffer, frame_data.object_allocation,
                             (void*&)frame_data.mapped_object_data);
        make_bindless(frame_data.object_buffer, frame_data.object_bindless_id);

        frame_data.indirect_buffer = VK_NULL_HANDLE;
        frame_data.indirect_size = 0;

        frame_data.draw_counts_buffer = VK_NULL_HANDLE;
        frame_data.draw_counts_size = 0;

        create_mapped_buffer(MAX_MATERIAL_BUFFER_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, frame_data.material_buffer,
                             frame_data.material_allocation, (void*&)frame_data.mapped_material_data);
        make_bindless(frame_data.material_buffer, frame_data.material_bindless_id);

        VkSemaphoreCreateInfo sem_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(ctx.device, &sem_info, nullptr, &frame_data.swapchain_acquire_semaphore));

        create_mapped_buffer(sizeof(gpu_directional_light_t) * MAX_DIR_LIGHTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             frame_data.dir_light_buffer, frame_data.dir_light_allocation,
                             (void*&)frame_data.mapped_dir_lights);
        make_bindless(frame_data.dir_light_buffer, frame_data.dir_light_bindless_id);

        create_mapped_buffer(sizeof(gpu_point_light_t) * MAX_LIGHTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             frame_data.point_light_buffer, frame_data.point_light_allocation,
                             (void*&)frame_data.mapped_point_lights);
        make_bindless(frame_data.point_light_buffer, frame_data.point_light_bindless_id);

        create_mapped_buffer(sizeof(gpu_spot_light_t) * MAX_LIGHTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             frame_data.spot_light_buffer, frame_data.spot_light_allocation,
                             (void*&)frame_data.mapped_spot_lights);
        make_bindless(frame_data.spot_light_buffer, frame_data.spot_light_bindless_id);

        frame_data.frame_allocator.init(2048 * 1024);
    }

    void shutdown_per_frame(per_frame_t& frame_data)
    {
        frame_data.transient_pool.shutdown();

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

        if (frame_data.object_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(ctx.allocator, frame_data.object_buffer, frame_data.object_allocation);
            res_system.buffer_heap.release(frame_data.object_bindless_id);
        }

        if (frame_data.indirect_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(ctx.allocator, frame_data.indirect_buffer, frame_data.indirect_alloc);
        }

        if (frame_data.draw_counts_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(ctx.allocator, frame_data.draw_counts_buffer, frame_data.draw_counts_alloc);
        }

        if (frame_data.material_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(ctx.allocator, frame_data.material_buffer, frame_data.material_allocation);
            res_system.buffer_heap.release(frame_data.material_bindless_id);
        }

        if (frame_data.dir_light_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(ctx.allocator, frame_data.dir_light_buffer, frame_data.dir_light_allocation);
            res_system.buffer_heap.release(frame_data.dir_light_bindless_id);
        }
        if (frame_data.point_light_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(ctx.allocator, frame_data.point_light_buffer, frame_data.point_light_allocation);
            res_system.buffer_heap.release(frame_data.point_light_bindless_id);
        }
        if (frame_data.spot_light_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(ctx.allocator, frame_data.spot_light_buffer, frame_data.spot_light_allocation);
            res_system.buffer_heap.release(frame_data.spot_light_bindless_id);
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

    void set_render_resolution(u32_t width, u32_t height)
    {
        if (width > 0 && height > 0)
        {
            ctx.logical_extent.width = width;
            ctx.logical_extent.height = height;

            if (ctx.use_offscreen_viewport)
            {
                ctx.render_extent.width = width;
                ctx.render_extent.height = height;
            }
        }
    }

    u32_t get_viewport_texture_id() { return ctx.viewport_bindless_id; }

    void set_use_offscreen_viewport(bool value)
    {
        ctx.use_offscreen_viewport = value;
        if (value && (ctx.render_extent.width == 0 || ctx.render_extent.height == 0))
        {
            ctx.render_extent = ctx.swapchain.extent;
        }
    }
} // namespace smol::renderer