#include "capsule_collider.h"

namespace smol::components
{
    void capsule_collider_ct::start()
    {
        JPH::CapsuleShapeSettings body_shape_settings(height, radius);
        body_shape_settings.SetDensity(1000.0);
        JPH::ShapeSettings::ShapeResult body_shape_result = body_shape_settings.Create();
        shape = body_shape_result.Get();

        collider_ct::start();
    }

    void capsule_collider_ct::set_height_radius(f32 h, f32 r)
    {
        height = (h - 2.0f * r) * 0.5f;
        radius = r;
    }

    JPH::Shape* capsule_collider_ct::get_shape() const
    {
        return shape.GetPtr();
    }
} // namespace smol::components
