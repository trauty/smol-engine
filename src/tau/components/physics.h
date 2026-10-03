#pragma once

#include "tau/defines.h"
#include "tau/math.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

namespace tau
{
    enum class body_type_e : u8_t
    {
        STATIC,
        KINEMATIC,
        DYNAMIC
    };

    struct TAU_ENGINE_API rigidbody_t
    {
        JPH::BodyID body_id;

        body_type_e type = body_type_e::DYNAMIC;
        bool is_sensor = false;
        bool is_initiaklized = false;

        body_type_e applied_type = body_type_e::DYNAMIC;
        bool applied_is_sensor = false;
        u64_t applied_shape_id = 0;
    };

    struct TAU_ENGINE_API box_collider_t
    {
        vec3_t extents = {0.5f, 0.5f, 0.5f};
        vec3_t offset = {0.0f, 0.0f, 0.0f};
    };

    struct TAU_ENGINE_API sphere_collider_t
    {
        f32 radius = 0.5f;
    };
} // namespace tau