#include "engine.h"

#include "asset/asset_manager.h"
#include "core/level.h"
#include "defines.h"
#include "log.h"
#include "main_thread.h"
#include "physics.h"
#include "rendering/renderer.h"
#include "time_util.h"
#include "window.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>
#include <glad/gl.h>

using namespace smol::asset;
using namespace smol::asset_manager;

namespace smol::engine
{
    namespace
    {
        std::shared_ptr<smol::core::level_t> current_level;
    }

    int init(const std::string& game_name, i32 init_window_width, i32 init_window_height)
    {
        smol::log::start();
        smol::log::set_level(smol::log::level_e::LOG_DEBUG);
        smol::asset_manager::init();
        smol::physics::init();

        SMOL_LOG_INFO("ENGINE", "Starting engine.");

        SDL_SetHintWithPriority(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1", SDL_HintPriority::SDL_HINT_OVERRIDE);
        SDL_Init(SDL_INIT_VIDEO);

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        SDL_Window* window = SDL_CreateWindow(game_name.c_str(), init_window_width, init_window_height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_GL_CreateContext(window);
        smol::window::set_window(window);

        if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress))
        {
            SMOL_LOG_FATAL("INIT", "Could not load OpenGL functions!");
            return -1;
        }

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CW);

        glViewport(0, 0, init_window_width, init_window_height);
        smol::renderer::init();

        return 0;
    }

    void run()
    {
        current_level->start();

        constexpr f64 fixed_timestep = 1.0 / 60.0; // this should be in a settings menu later on
        f64 current_time = smol::time::get_time_in_seconds();
        f64 accumulator = 0.0;

        SDL_Window* window = smol::window::get_window();

        bool is_running = true;

        while (is_running)
        {
            const f64 new_time = smol::time::get_time_in_seconds();
            f64 frame_time = new_time - current_time;

            if (frame_time >= 0.25)
            {
                frame_time = 0.25;
            }

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
                    case SDL_EVENT_QUIT:
                        is_running = false;
                        break;
                    case SDL_EVENT_WINDOW_RESIZED:
                        smol::window::set_window_size(event.window.data1, event.window.data2);
                        break;
                    default:
                        break;
                }
            }

            // REFACTOR: should be in render
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            smol::renderer::render();

            SDL_GL_SwapWindow(window);
        }
    }

    int shutdown()
    {
        SMOL_LOG_INFO("ENGINE", "Stopping engine.");

        if (current_level != nullptr)
        {
            current_level = nullptr;
        }

        smol::renderer::shutdown();
        smol::physics::shutdown();
        smol::asset_manager::shutdown();
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
        current_level = level;
        return SMOL_SUCCESS;
    }
} // namespace smol::engine