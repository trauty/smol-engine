#pragma once

#include "collider.h"
#include "smol/math_util.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

using namespace smol::math;

namespace smol::components
{
    class box_collider_ct : public collider_ct
    {
      public:
        void start() override;

        JPH::Shape* get_shape() const override;
        void set_size(vec3_t size);

      private:
        vec3_t size = {1.0f, 1.0f, 1.0f};
    };
} // namespace smol::components
