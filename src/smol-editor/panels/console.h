#pragma once

#include "smol-editor/editor_context.h"
#include "smol/world.h"

namespace smol::editor::panels
{
    void draw_console(world_t& world, editor_context_t& ctx);

    // the console's lines alone, filling the space left, for the wait screen before the panel exists
    void draw_log_view();
} // namespace smol::editor::panels