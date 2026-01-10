#include "engine.h"

#include "asset.h"
#include "core/level.h"
#include "defines.h"
#include "log.h"
#include "main_thread.h"
#include "physics.h"
#include "rendering/renderer.h"
#include "smol/rendering/shader_compiler.h"
#include "smol/util.h"
#include "time_util.h"
#include "window.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>

#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#ifdef NDEBUG
const bool enable_validation_layers = false;
#else
const bool enable_validation_layers = true;
#endif

const std::vector<const char*> validation_layers = {"VK_LAYER_KHRONOS_validation"};

namespace smol::engine
{
    namespace
    {
        std::shared_ptr<smol::core::level_t> current_level;
        VkDebugUtilsMessengerEXT debug_messenger;
        PFN_vkCreateDebugUtilsMessengerEXT pfn_vkCreateDebugUtilsMessengerEXT = nullptr;
        PFN_vkDestroyDebugUtilsMessengerEXT pfn_vkDestroyDebugUtilsMessengerEXT = nullptr;

        static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
            VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type,
            const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data, void* ptr_user_data)
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
    } // namespace

    int init(const std::string& game_name, i32 init_window_width, i32 init_window_height)
    {
        smol::log::init();
        smol::log::set_level(smol::log::level_e::LOG_DEBUG);
        smol::asset_manager_t::init();
        smol::physics::init();

        SMOL_LOG_INFO("ENGINE", "Starting engine.");

        SDL_SetHintWithPriority(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1", SDL_HintPriority::SDL_HINT_OVERRIDE);
        SDL_Init(SDL_INIT_VIDEO);

        SDL_Window* window = SDL_CreateWindow(game_name.c_str(), init_window_width, init_window_height,
                                              SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        smol::window::set_window(window);

        VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app_info.pNext = nullptr;
        app_info.pApplicationName = game_name.c_str();
        app_info.apiVersion = VK_API_VERSION_1_2;

        u32 sdl_ext_count = 0;
        const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);
        std::vector<const char*> extensions(sdl_extensions, sdl_extensions + sdl_ext_count);
        if (enable_validation_layers) { extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); }
        for (const char* ext : extensions) { SMOL_LOG_INFO("VULKAN EXT", "{}", ext); }

        VkInstanceCreateInfo create_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        create_info.pApplicationInfo = &app_info;
        create_info.enabledExtensionCount = static_cast<u32>(extensions.size());
        create_info.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {};
        if (enable_validation_layers)
        {
            create_info.enabledLayerCount = static_cast<u32>(validation_layers.size());
            create_info.ppEnabledLayerNames = validation_layers.data();

            debug_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debug_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug_create_info.pfnUserCallback = debug_callback;

            create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debug_create_info;
        }
        else
        {
            create_info.enabledLayerCount = 0;
            create_info.pNext = nullptr;
        }

        VK_CHECK(vkCreateInstance(&create_info, nullptr, &renderer::ctx::instance));

        if (enable_validation_layers)
        {
            bool loaded = true;
            loaded &= smol::util::load_vulkan_function(renderer::ctx::instance, "vkCreateDebugUtilsMessengerEXT",
                                                       pfn_vkCreateDebugUtilsMessengerEXT);
            loaded &= smol::util::load_vulkan_function(renderer::ctx::instance, "vkDestroyDebugUtilsMessengerEXT",
                                                       pfn_vkDestroyDebugUtilsMessengerEXT);

            if (loaded && pfn_vkCreateDebugUtilsMessengerEXT(renderer::ctx::instance, &debug_create_info, nullptr,
                                                             &debug_messenger) != VK_SUCCESS)
            {
                SMOL_LOG_ERROR("VULKAN", "Failed to set up validation layer logger");
            }
        }

        if (!SDL_Vulkan_CreateSurface(window, renderer::ctx::instance, nullptr, &renderer::ctx::surface))
        {
            SMOL_LOG_FATAL("ENGINE", "Could not create SDL Vulkan surface");
            return -1;
        }

        u32 device_count = 0;
        vkEnumeratePhysicalDevices(renderer::ctx::instance, &device_count, nullptr);
        std::vector<VkPhysicalDevice> devices(device_count);
        vkEnumeratePhysicalDevices(renderer::ctx::instance, &device_count, devices.data());
        renderer::ctx::physical_device = devices[0]; // just takes the first gpu, needs to be selectable

        f32 queue_priority = 1.0f;
        VkDeviceQueueCreateInfo queue_create_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_create_info.pNext = nullptr;
        // assuming first family of queues can present and do graphics (not good)
        queue_create_info.queueFamilyIndex = 0;
        queue_create_info.queueCount = 1;
        queue_create_info.pQueuePriorities = &queue_priority;

        const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo device_create_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_create_info.pNext = nullptr;
        device_create_info.pQueueCreateInfos = &queue_create_info;
        device_create_info.queueCreateInfoCount = 1;
        device_create_info.enabledExtensionCount = 1;
        device_create_info.ppEnabledExtensionNames = device_extensions;

        VK_CHECK(vkCreateDevice(renderer::ctx::physical_device, &device_create_info, nullptr, &renderer::ctx::device));
        vkGetDeviceQueue(renderer::ctx::device, 0, 0, &renderer::ctx::graphics_queue);
        renderer::ctx::present_queue = renderer::ctx::graphics_queue;

        renderer::ctx::swapchain_format = VK_FORMAT_B8G8R8A8_SRGB;

        VkAttachmentDescription color_attachment = {};
        color_attachment.format = renderer::ctx::swapchain_format;
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT; // no msaa for now
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_attachment_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkAttachmentDescription depth_attachment = {};
        depth_attachment.format = renderer::find_depth_format();
        depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depth_attachment_ref = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment_ref;
        subpass.pDepthStencilAttachment = &depth_attachment_ref;

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
        render_pass_info.pNext = nullptr;
        render_pass_info.attachmentCount = 2;
        render_pass_info.pAttachments = attachments;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &subpass_dep;

        VK_CHECK(vkCreateRenderPass(renderer::ctx::device, &render_pass_info, nullptr, &renderer::ctx::render_pass));

        renderer::create_swapchain();

        VkCommandPoolCreateInfo cmd_pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cmd_pool_info.pNext = nullptr;
        cmd_pool_info.queueFamilyIndex = 0;
        cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK(vkCreateCommandPool(renderer::ctx::device, &cmd_pool_info, nullptr, &renderer::ctx::command_pool));

        renderer::ctx::command_buffers.resize(renderer::ctx::MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc_info.pNext = nullptr;
        alloc_info.commandPool = renderer::ctx::command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = (u32)renderer::ctx::command_buffers.size();
        VK_CHECK(vkAllocateCommandBuffers(renderer::ctx::device, &alloc_info, renderer::ctx::command_buffers.data()));

        renderer::ctx::image_available_semaphores.resize(renderer::ctx::MAX_FRAMES_IN_FLIGHT);
        renderer::ctx::render_finished_semaphores.resize(renderer::ctx::swapchain_images.size());
        renderer::ctx::in_flight_fences.resize(renderer::ctx::MAX_FRAMES_IN_FLIGHT);
        renderer::ctx::images_in_flight.resize(renderer::ctx::swapchain_images.size(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo sem_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        sem_info.pNext = nullptr;
        VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_info.pNext = nullptr;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (u32 i = 0; i < renderer::ctx::MAX_FRAMES_IN_FLIGHT; i++)
        {
            VK_CHECK(vkCreateSemaphore(renderer::ctx::device, &sem_info, nullptr,
                                       &renderer::ctx::image_available_semaphores[i]));
            VK_CHECK(vkCreateFence(renderer::ctx::device, &fence_info, nullptr, &renderer::ctx::in_flight_fences[i]));
        }
        for (u32 i = 0; i < renderer::ctx::swapchain_images.size(); i++)
        {
            VK_CHECK(vkCreateSemaphore(renderer::ctx::device, &sem_info, nullptr,
                                       &renderer::ctx::render_finished_semaphores[i]));
        }

        VkDescriptorSetLayoutBinding global_layout_binding = {};
        global_layout_binding.binding = 0;
        global_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        global_layout_binding.descriptorCount = 1;
        global_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout_info.pNext = nullptr;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &global_layout_binding;
        VK_CHECK(vkCreateDescriptorSetLayout(renderer::ctx::device, &layout_info, nullptr,
                                             &renderer::ctx::global_set_layout));

        // currently arbitrary size
        VkDescriptorPoolSize pool_sizes[3] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                                              {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                                              {VK_DESCRIPTOR_TYPE_SAMPLER, 1000}};

        VkDescriptorPoolCreateInfo pool_create_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_create_info.pNext = nullptr;
        pool_create_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_create_info.poolSizeCount = 3;
        pool_create_info.pPoolSizes = pool_sizes;
        pool_create_info.maxSets = 2000;
        VK_CHECK(
            vkCreateDescriptorPool(renderer::ctx::device, &pool_create_info, nullptr, &renderer::ctx::descriptor_pool));

        smol::shader_compiler::init();
        smol::renderer::init();

        return 0;
    }

    void run()
    {
        current_level->start();

        constexpr f64 fixed_timestep = 1.0 / 60.0; // this should be in a settings file later on
        f64 current_time = smol::time::get_time_in_seconds();
        f64 accumulator = 0.0;

        bool is_running = true;

        while (is_running)
        {
            const f64 new_time = smol::time::get_time_in_seconds();
            f64 frame_time = new_time - current_time;

            if (frame_time >= 0.25) { frame_time = 0.25; }

            current_time = new_time;
            accumulator += frame_time;

            while (accumulator >= fixed_timestep)
            {
                current_level->fixed_update(fixed_timestep);
                accumulator -= fixed_timestep;
            }
            smol::physics::update(frame_time);
            smol::physics::interpolation_alpha = static_cast<f32>(accumulator / fixed_timestep);
            current_level->update(frame_time);

            smol::main_thread::execute();

            static SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                switch (event.type)
                {
                    case SDL_EVENT_QUIT: is_running = false; break;
                    case SDL_EVENT_WINDOW_RESIZED:
                        smol::window::set_window_size(event.window.data1, event.window.data2);
                        break;
                    default: break;
                }
            }

            smol::renderer::render();
        }
    }

    int shutdown()
    {
        SMOL_LOG_INFO("ENGINE", "Stopping engine.");

        vkDeviceWaitIdle(renderer::ctx::device);

        if (current_level != nullptr) { current_level = nullptr; }

        smol::asset_manager_t::clear_all();
        smol::asset_manager_t::shutdown();

        smol::renderer::shutdown();

        if (enable_validation_layers && pfn_vkDestroyDebugUtilsMessengerEXT)
        {
            pfn_vkDestroyDebugUtilsMessengerEXT(renderer::ctx::instance, debug_messenger, nullptr);
        }

        if (renderer::ctx::instance != VK_NULL_HANDLE) { vkDestroyInstance(renderer::ctx::instance, nullptr); }

        smol::physics::shutdown();
        smol::window::shutdown();
        smol::log::shutdown();
        return 0;
    }

    void exit()
    {
        SDL_Event quit_event = {.type = SDL_EVENT_QUIT};
        SDL_PushEvent(&quit_event);
    }

    smol_result_e load_level(std::shared_ptr<smol::core::level_t> level)
    {
        current_level = std::move(level);
        return SMOL_SUCCESS;
    }
} // namespace smol::engine