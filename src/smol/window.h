#pragma once

#include "defines.h"
#include "smol/ecs_types.h"

struct SDL_Window;

namespace smol::window
{
    struct window_size_changed_event
    {
        SMOL_COMPONENT(window_size_changed_event)

        i32_t width;
        i32_t height;
    };

    void set_window(SDL_Window* new_window);
    SDL_Window* get_window();
    SMOL_API void get_window_size(i32* width, i32* height);
    SMOL_API void set_window_size(i32 width, i32 height);
    SMOL_API void set_window_position(i32 pos_x, i32 pos_y);
    void shutdown();
} // namespace smol::window