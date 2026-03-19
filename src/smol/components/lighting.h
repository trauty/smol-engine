#pragma once

#include "smol/math.h"

namespace smol
{
    struct directional_light_t
    {
        vec3_t color = {1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
    };

    struct point_light_t
    {
        vec3_t color = {1.0f, 1.0f, 1.0f};
        float intensity = 10.0f;
        float radius = 10.0f;
    };

    struct spot_light_t
    {
        vec3_t color = {1.0f, 1.0f, 1.0f};
        float intensity = 15.0f;
        float radius = 20.0f;
        float inner_angle = 12.5f;
        float outer_angle = 17.5f;
    };
} // namespace smol