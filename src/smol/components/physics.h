#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include "smol/defines.h"
#include "smol/math.h"

namespace smol
{
    enum class body_type_e : u8_t
    {
        STATIC,
        KINEMATIC,
        DYNAMIC
    };

    struct rigidbody_t
    {
        JPH::BodyID body_id;

        body_type_e type = body_type_e::DYNAMIC;
        bool is_sensor = false;
        bool is_initiaklized = false;
    };

    struct box_collider_t
    {
        vec3_t extents = {0.5f, 0.5f, 0.5f};
        vec3_t offset = {0.0f, 0.0f, 0.0f};
    };

    struct sphere_collider_t
    {
        f32 radius = 0.5f;
    };
} // namespace smol