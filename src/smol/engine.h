#pragma once

#include "defines.h"
#include "smol/asset_fwd.h"
#include "smol/world.h"

#include <functional>
#include <memory>

union SDL_Event;

namespace smol::engine
{
    using event_callback_t = std::function<bool(const SDL_Event&)>;
    using ui_callback_t = std::function<void()>;

    SMOL_API bool init(const std::string& game_name, i32 init_window_width, i32 init_window_height);
    SMOL_API void run();
    SMOL_API bool shutdown();

    SMOL_API void exit();

    SMOL_API void create_scene();
    SMOL_API void set_scene(std::unique_ptr<smol::world_t> scene);
    SMOL_API world_t& get_active_world();
    SMOL_API asset_registry_t& get_asset_registry();

    SMOL_API void set_event_callback(event_callback_t cb);
    SMOL_API void set_ui_callback(ui_callback_t cb);
} // namespace smol::engine