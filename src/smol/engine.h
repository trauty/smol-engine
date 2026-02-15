#pragma once

#include "defines.h"
#include "smol/ecs.h"
#include "smol/world.h"

#include <memory>
#include <string>

namespace smol::engine
{
    SMOL_API int init(const std::string& game_name, i32 init_window_width, i32 init_window_height);
    SMOL_API void run();
    SMOL_API int shutdown();

    SMOL_API void exit();

    SMOL_API void set_scene(std::unique_ptr<smol::world_t> scene);
    SMOL_API world_t& get_active_world();
} // namespace smol::engine