#pragma once

#include <imgui/imgui.h>

namespace smol::editor::imgui
{
    void init();
    void shutdown();
    void submit(ImDrawData* draw_data);
} // namespace smol::editor::imgui