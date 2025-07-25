#pragma once

#include "defines.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "math_util.h"
#include "core/component.h"

using namespace smol::math;

namespace smol::components
{
    class rigidbody_ct : public smol::core::component_t
    {
    public:
        rigidbody_ct() = default;
        ~rigidbody_ct();

        void start() override;
        void update(f64 delta_time) override;
        void fixed_update(f64 fixed_timestep) override;

        void add_force(const vec3_t& force);
        void lock_axis(bool x_axis, bool y_axis, bool z_axis);

        bool is_kinematic = false;

    private:
        JPH::BodyID body_id;
        JPH::Body* body = nullptr;

        bool locked_x = false, locked_y = false, locked_z = false;
    };
}