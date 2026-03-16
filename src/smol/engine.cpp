#include "engine.h"

#include "smol/asset_registry.h"
#include "smol/defines.h"
#include "smol/jobs.h"
#include "smol/log.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/shader_compiler.h"
#include "smol/systems/camera.h"
#include "smol/systems/events.h"
#include "smol/systems/transform.h"
#include "smol/time.h"
#include "smol/window.h"
#include "smol/world.h"

// clang-format off
#include "smol/rendering/vulkan.h"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <tracy/Tracy.hpp>
#include <memory>
#include <utility>
#include <vector>
// clang-format on

#ifdef NDEBUG
const bool enable_validation_layers = false;
#else
const bool enable_validation_layers = true;
#endif

namespace smol::engine
{
    namespace
    {
        std::unique_ptr<world_t> active_scene;
        asset_registry_t engine_assets;
        bool is_running = true;
    } // namespace

    bool init(const std::string& game_name, i32 init_window_width, i32 init_window_height)
    {
        smol::log::init();
        smol::log::set_level(smol::log::level_e::LOG_DEBUG);

        SMOL_LOG_INFO("ENGINE", "Starting engine...");

        smol::jobs::init();

        // SDL_SetHintWithPriority(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1", SDL_HintPriority::SDL_HINT_OVERRIDE);
        SDL_Init(SDL_INIT_VIDEO);

        if (volkInitialize() != VK_SUCCESS) {}

        SDL_Window* window = SDL_CreateWindow(game_name.c_str(), init_window_width, init_window_height,
                                              SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        smol::window::set_window(window);

        u32 sdl_ext_count = 0;
        const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);
        std::vector<const char*> extensions(sdl_extensions, sdl_extensions + sdl_ext_count);

        renderer::context_config_t renderer_config = {
            .app_name = game_name,
            .enable_validation = enable_validation_layers,
            .required_instance_exts = std::move(extensions),
        };

        if (!smol::renderer::init(renderer_config, window))
        {
            SMOL_LOG_FATAL("ENGINE", "Failed to initialize renderer, aborting...");
            return false;
        }

        smol::shader_compiler::init();
        return true;
    }

    void run()
    {
        constexpr f64 fixed_timestep = 1.0 / 60.0; // this should be in a settings file later on
        smol::time::fixed_dt = fixed_timestep;
        f64 current_time = smol::time::time;
        f64 accumulator = 0.0;

        while (is_running)
        {
            smol::time::update();
            const f64 new_time = smol::time::time;
            f64 frame_time = new_time - current_time;

            if (frame_time >= 0.25) { frame_time = 0.25; }

            current_time = new_time;
            accumulator += frame_time;

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

            while (accumulator >= fixed_timestep)
            {
                active_scene->fixed_update();
                accumulator -= fixed_timestep;
            }

            // smol::physics::interpolation_alpha = static_cast<f32>(accumulator / fixed_timestep);
            active_scene->update();

            smol::transform_system::update(active_scene->registry);
            smol::camera_system::update(active_scene->registry);

            smol::renderer::render(active_scene->registry);

            smol::event_system::clear_frame_events(active_scene->registry);

            FrameMark;
        }
    }

    bool shutdown()
    {
        SMOL_LOG_INFO("ENGINE", "Stopping engine.");

        vkDeviceWaitIdle(renderer::ctx.device);

        if (active_scene)
        {
            active_scene->shutdown();
            active_scene.reset();
        }

        engine_assets.shutdown();

        smol::renderer::shutdown();

        smol::jobs::shutdown();

        smol::window::shutdown();
        smol::log::shutdown();
        return 0;
    }

    void exit() { is_running = false; }

    void create_scene()
    {
        if (active_scene) { active_scene->shutdown(); }

        active_scene = std::make_unique<smol::world_t>();

        if (!active_scene) { SMOL_LOG_ERROR("ENGINE", "Could not create scene"); }
    }

    void set_scene(std::unique_ptr<smol::world_t> new_scene)
    {
        if (active_scene) { active_scene->shutdown(); }

        active_scene = std::move(new_scene);

        if (active_scene) { active_scene->init(); }
        else
        {
            SMOL_LOG_ERROR("ENGINE", "Could not set scene");
        }
    }

    world_t& get_active_world() { return *active_scene; }

    asset_registry_t& get_asset_registry() { return engine_assets; }
} // namespace smol::engine