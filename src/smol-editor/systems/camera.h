#pragma once

#include "smol/ecs_fwd.h"
namespace smol::editor
{
    struct editor_camera_tag
    {
    };

    namespace camera_system { void update(ecs::registry_t& reg, bool is_viewport_hovered); }
} // namespace smol::editor