#pragma once

#include <imgui/imgui.h>

namespace tau::editor::imgui
{
    void init();
    void init_multiviewport();
    void shutdown();
    void submit(ImDrawData* draw_data);
} // namespace tau::editor::imgui