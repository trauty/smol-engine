#pragma once

#include "smol/defines.h"

#include <imgui/imgui.h>

namespace smol::renderer::imgui
{
    SMOL_API void init();
    SMOL_API void shutdown();
    SMOL_API void submit(ImDrawData* draw_data);
} // namespace smol::renderer::imgui