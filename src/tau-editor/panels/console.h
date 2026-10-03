#pragma once

#include "tau-editor/editor_context.h"
#include "tau/world.h"

namespace tau::editor::panels
{
    void draw_console(world_t& world, editor_context_t& ctx);

    // the console's lines alone, filling the space left, for the wait screen before the panel exists
    void draw_log_view();
} // namespace tau::editor::panels