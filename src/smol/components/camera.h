#pragma once

#include "smol/defines.h"
#include "smol/math.h"

namespace smol
{
    struct active_camera_tag
    {
    };

    struct camera_t
    {
        f32 fov_deg = 45.0f;
        f32 near_plane = 0.1f;
        f32 far_plane = 1000.0f;
        f32 aspect = 16.0f / 9.0f;

        mat4_t view;
        mat4_t projection;
        mat4_t view_proj;

        bool is_dirty = false;
    };
} // namespace smol