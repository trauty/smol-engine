#pragma once

#include <imgui/imgui.h>

namespace smol::editor::imgui
{
    void init();
    void init_multiviewport();
    void shutdown();
    void submit(ImDrawData* draw_data);
} // namespace smol::editor::imgui