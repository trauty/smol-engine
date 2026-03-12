#include "renderer.h"

#include "SDL_error.h"
#include "SDL_vulkan.h"
#include "cglm/euler.h"
#include "cglm/quat.h"
#include "smol/assets/mesh.h"
#include "smol/assets/shader.h"
#include "smol/components/camera.h"
#include "smol/components/renderer.h"
#include "smol/components/transform.h"
#include "smol/defines.h"
#include "smol/log.h"
#include "smol/math.h"
#include "smol/rendering/material.h"
#include "smol/rendering/renderer_resources.h"
#include "smol/rendering/renderer_types.h"
#include "smol/util.h"
#include "smol/window.h"

#include <SDL3/SDL_video.h>
#include <algorithm>
#include <cglm/mat4.h>
#include <cglm/types.h>
#include <cglm/vec3.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <set>
#include <tracy/Tracy.hpp>
#include <vector>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

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

    // TEMP
    VkShaderModule create_shader_module(const std::vector<char>& code)
    {
        VkShaderModuleCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        create_info.codeSize = code.size();
        create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module;
        VK_CHECK(vkCreateShaderModule(ctx.device, &create_info, nullptr, &module));
        return module;
    }

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
        app_info.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instance_info.pApplicationInfo = &app_info;
        instance_info.enabledExtensionCount = static_cast<u32_t>(instance_exts.size());
        instance_info.ppEnabledExtensionNames = instance_exts.data();
        instance_info.enabledLayerCount = enable_validation ? static_cast<u32_t>(validation_layers.size()) : 0;
        instance_info.ppEnabledLayerNames = enable_validation ? validation_layers.data() : nullptr;
        instance_info.pNext = enable_validation ? &debug_create_info : nullptr;

        VK_CHECK(vkCreateInstance(&instance_info, nullptr, &ctx.instance));

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

        VkPhysicalDeviceFeatures2 device_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        device_features_check.pNext = &timeline_sem_features;
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

        for (const char* extension : device_exts) { ctx.active_device_exts.push_back(extension); }

        VmaVulkanFunctions vulkan_funcs = {};
        vulkan_funcs.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
        vulkan_funcs.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo allocator_info = {};
        // allocator_info.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
        allocator_info.vulkanApiVersion = VK_API_VERSION_1_2;
        allocator_info.physicalDevice = ctx.physical_device;
        allocator_info.device = ctx.device;
        allocator_info.instance = ctx.instance;
        allocator_info.pVulkanFunctions = &vulkan_funcs;

        VK_CHECK(vmaCreateAllocator(&allocator_info, &ctx.allocator));

        vkGetDeviceQueue(ctx.device, ctx.queue_fam_indices.graphics_family.value(), 0, &ctx.graphics_queue);
        vkGetDeviceQueue(ctx.device, ctx.queue_fam_indices.present_family.value(), 0, &ctx.present_queue);
        vkGetDeviceQueue(ctx.device, ctx.queue_fam_indices.transfer_family.value(), 0, &ctx.transfer_queue);

        // swapchain setup

        VkAttachmentDescription color_attachment = {};
        color_attachment.format = select_surface_format(ctx.physical_device, ctx.surface).format;
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depth_attachment = {};
        depth_attachment.format = find_depth_format();
        depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // TEMP: till render graph works, i still need to implement bindless
        // descriptors...
        VkAttachmentReference attachment_refs[] = {
            {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL        },
            {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}
        };

        VkSubpassDescription subpass_desc = {};
        subpass_desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass_desc.colorAttachmentCount = 1;
        subpass_desc.pColorAttachments = &attachment_refs[0];
        subpass_desc.pDepthStencilAttachment = &attachment_refs[1];

        VkSubpassDependency subpass_dep = {};
        subpass_dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        subpass_dep.dstSubpass = 0;
        subpass_dep.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        subpass_dep.srcAccessMask = 0;
        subpass_dep.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        subpass_dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkAttachmentDescription attachments[] = {color_attachment, depth_attachment};

        VkRenderPassCreateInfo render_pass_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        render_pass_info.attachmentCount = 2;
        render_pass_info.pAttachments = attachments;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass_desc;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &subpass_dep;

        VK_CHECK(vkCreateRenderPass(ctx.device, &render_pass_info, nullptr, &ctx.main_render_pass));

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

        init_resources();

        // TEST PIPELINE -- VERY TEMPORARY
        VkSamplerCreateInfo sampler_info = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.anisotropyEnable = VK_TRUE;
        sampler_info.maxAnisotropy = ctx.properties.limits.maxSamplerAnisotropy;
        sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        sampler_info.unnormalizedCoordinates = VK_FALSE;
        sampler_info.compareEnable = VK_FALSE;
        sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        VK_CHECK(vkCreateSampler(ctx.device, &sampler_info, nullptr, &ctx.test_sampler));

        VkDescriptorImageInfo sampler_desc_info = {.sampler = ctx.test_sampler};
        VkWriteDescriptorSet sampler_write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        sampler_write.dstSet = res_system.global_set;
        sampler_write.dstBinding = 0;
        sampler_write.dstArrayElement = 0;
        sampler_write.descriptorCount = 1;
        sampler_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        sampler_write.pImageInfo = &sampler_desc_info;
        vkUpdateDescriptorSets(ctx.device, 1, &sampler_write, 0, nullptr);

        auto vert_code = util::read_file_raw("assets/shaders/test.vert.spv");
        auto frag_code = util::read_file_raw("assets/shaders/test.frag.spv");
        VkShaderModule vert_module = create_shader_module(vert_code);
        VkShaderModule frag_module = create_shader_module(frag_code);

        VkPipelineShaderStageCreateInfo shader_stages[] = {
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_VERTEX_BIT,
             .module = vert_module,
             .pName = "main"},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
             .module = frag_module,
             .pName = "main"}
        };

        VkVertexInputBindingDescription binding_desc = {0, sizeof(vertex_t), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attribute_desc[] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex_t, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex_t, normal)  },
            {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(vertex_t, uv)      }
        };

        // bypassing ia
        VkPipelineVertexInputStateCreateInfo vertex_input_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 0,
            .pVertexBindingDescriptions = nullptr,
            .vertexAttributeDescriptionCount = 0,
            .pVertexAttributeDescriptions = nullptr};

        VkPipelineInputAssemblyStateCreateInfo input_assembly = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE};
        VkPipelineViewportStateCreateInfo viewport_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
        VkPipelineRasterizationStateCreateInfo rasterizer = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .lineWidth = 1.0f};
        VkPipelineMultisampleStateCreateInfo multisampling = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE};
        VkPipelineDepthStencilStateCreateInfo depth_stencil = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE};
        VkPipelineColorBlendAttachmentState color_blend_attachment = {
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT};
        VkPipelineColorBlendStateCreateInfo color_blending = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .attachmentCount = 1,
            .pAttachments = &color_blend_attachment};
        VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                          .dynamicStateCount = 2,
                                                          .pDynamicStates = dynamic_states};

        VkPushConstantRange push_constant = {
            .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS, .offset = 0, .size = sizeof(uint32_t) * 4};

        VkPipelineLayoutCreateInfo pipeline_layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &res_system.global_layout;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &push_constant;

        VK_CHECK(vkCreatePipelineLayout(ctx.device, &pipeline_layout_info, nullptr, &ctx.test_pipeline_layout));

        VkGraphicsPipelineCreateInfo pipeline_info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = shader_stages;
        pipeline_info.pVertexInputState = &vertex_input_info;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pDepthStencilState = &depth_stencil;
        pipeline_info.pColorBlendState = &color_blending;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = ctx.test_pipeline_layout;
        pipeline_info.renderPass = ctx.main_render_pass;
        pipeline_info.subpass = 0;

        VK_CHECK(vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &ctx.test_pipeline));

        vkDestroyShaderModule(ctx.device, frag_module, nullptr);
        vkDestroyShaderModule(ctx.device, vert_module, nullptr);

        SMOL_LOG_INFO("VULKAN", "Context initialized for GPU: {}", ctx.properties.deviceName);
        return true;
    }

    void shutdown()
    {
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

        for (VkFramebuffer fb : ctx.swapchain.framebuffers) { vkDestroyFramebuffer(ctx.device, fb, nullptr); }

        for (per_frame_t& frame_data : ctx.per_frame_objects) { shutdown_per_frame(frame_data); }

        ctx.per_frame_objects.clear();

        for (VkSemaphore sem : ctx.recycled_semaphores) { vkDestroySemaphore(ctx.device, sem, nullptr); }

        if (ctx.main_render_pass != VK_NULL_HANDLE) { vkDestroyRenderPass(ctx.device, ctx.main_render_pass, nullptr); }

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
        VkFramebuffer fb = ctx.swapchain.framebuffers[index];
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
            {.color = {{0.2f, 0.2f, 0.2f, 1.0f}}},
            {.depthStencil = {1.0f, 0}},
        };

        VkRenderPassBeginInfo rp_begin_info = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = ctx.main_render_pass,
            .framebuffer = fb,
            .renderArea = {.extent = ctx.swapchain.extent},
            .clearValueCount = 2,
            .pClearValues = clear_values,
        };

        vkCmdBeginRenderPass(cmd, &rp_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp = {
            .width = static_cast<f32>(ctx.swapchain.extent.width),
            .height = static_cast<f32>(ctx.swapchain.extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(cmd, 0, 1, &vp);

        VkRect2D scissor = {.extent = ctx.swapchain.extent};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // VERY TEMPORARY
        if (ctx.active_mesh && ctx.active_tex)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.test_pipeline);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.test_pipeline_layout, 0, 1,
                                    &res_system.global_set, 0, nullptr);
            struct
            {
                uint32_t tex_id;
                uint32_t samp_id;
                uint32_t vtx_id;
                uint32_t idx_id;
            } pc_data = {ctx.active_tex->bindless_id, 0, ctx.active_mesh->vertex_bindless_id,
                         ctx.active_mesh->index_bindless_id};

            vkCmdPushConstants(cmd, ctx.test_pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(pc_data),
                               &pc_data);

            vkCmdDraw(cmd, ctx.active_mesh->index_count, 1, 0, 0);
        }

        vkCmdEndRenderPass(cmd);

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

        for (VkFramebuffer& fb : ctx.swapchain.framebuffers) { vkDestroyFramebuffer(ctx.device, fb, nullptr); }

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

        ctx.swapchain.framebuffers.clear();

        for (VkImageView& image_view : ctx.swapchain.views)
        {
            VkImageView attachments[] = {image_view, ctx.swapchain.depth_view};

            VkFramebufferCreateInfo fb_info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = ctx.main_render_pass,
                .attachmentCount = 2,
                .pAttachments = attachments,
                .width = ctx.swapchain.extent.width,
                .height = ctx.swapchain.extent.height,
                .layers = 1,
            };

            VkFramebuffer fb;
            VK_CHECK(vkCreateFramebuffer(ctx.device, &fb_info, nullptr, &fb));

            ctx.swapchain.framebuffers.push_back(fb);
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
                .bindless_id = NULL_HANDLE,
                .gpu_timeline_value = signal_value,
            });
        }

        return signal_value;
    }
} // namespace smol::renderer