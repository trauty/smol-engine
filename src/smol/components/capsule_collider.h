#pragma once

#include "collider.h"
#include "smol/math_util.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

using namespace smol::math;

namespace smol::components
{
    class capsule_collider_ct : public collider_ct
    {
      public:
        void start() override;

        JPH::Shape* get_shape() const override;
        void set_height_radius(f32 height, f32 radius);

      private:
        f32 height = 2.0f;
        f32 radius = 0.25f;
    };
} // namespace smol::components
