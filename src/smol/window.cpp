#include "window.h"

#include "smol/defines.h"
#include "smol/events.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>
#include <glad/gl.h>

namespace smol::window
{
    namespace
    {
        SDL_Window* window;
    }

    void set_window(SDL_Window* new_window)
    {
        window = new_window;
    }

    SDL_Window* get_window()
    {
        return window;
    }

    void get_window_size(i32* width, i32* height)
    {
        SDL_GetWindowSize(window, width, height);
    }

    void set_window_size(i32 width, i32 height)
    {
        SDL_SetWindowSize(window, width, height);
        glViewport(0, 0, width, height);
        smol::events::emit(size_changed_event_t {width, height});
    }

    void set_window_position(i32 pos_x, i32 pos_y)
    {
        SDL_SetWindowPosition(window, pos_x, pos_y);
    }

    void shutdown()
    {
        SDL_Quit();
    }
} // namespace smol::window