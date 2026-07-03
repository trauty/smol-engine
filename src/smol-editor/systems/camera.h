#pragma once

#include "smol/math.h"

namespace smol::editor
{
    struct editor_camera_t
    {
        vec3_t position = {0.0f, 0.0f, 5.0f};
        quat_t rotation;
        f32 yaw = 0.0f;
        f32 pitch = 0.0f;
        f32 fov_deg = 45.0f;
        f32 near_plane = 0.1f;
        f32 far_plane = 1000.0f;
    };

    namespace camera_system { void update(editor_camera_t& cam, bool is_viewport_hovered); }
} // namespace smol::editor
