#pragma once
#include "../core/component.h"

class point_light_ct : public smol::core::component_t
{
public:
    vec3_t color = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;

    void on_start() {}
    void on_update(float delta_time)  {}
};