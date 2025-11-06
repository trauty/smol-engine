#pragma once

#include "defines.h"

#include <memory>
#include <string>

namespace smol::core
{
    class level_t;
}

namespace smol::engine
{
    SMOL_API int init(const std::string& game_name, i32 init_window_width, i32 init_window_height);
    SMOL_API void run();
    SMOL_API int shutdown();

    SMOL_API void exit();

    SMOL_API smol_result_e load_level(std::shared_ptr<smol::core::level_t> level);
} // namespace smol::engine