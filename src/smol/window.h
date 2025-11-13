#pragma once

#include "defines.h"
#include "events.h"

struct SDL_Window;

namespace smol::window
{
    struct size_changed_event_t : smol::events::event_t
    {
        i32 width;
        i32 height;

        size_changed_event_t(i32 w, i32 h) : width(w), height(h) {}
    };

    void set_window(SDL_Window* new_window);
    SDL_Window* get_window();
    SMOL_API void get_window_size(i32* width, i32* height);
    SMOL_API void set_window_size(i32 width, i32 height);
    SMOL_API void set_window_position(i32 pos_x, i32 pos_y);
    void shutdown();
} // namespace smol::window