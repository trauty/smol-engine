#include "box_collider.h"

namespace smol::components
{
    void box_collider_ct::start()
    {
        JPH::BoxShapeSettings body_shape_settings(JPH::Vec3(size.x, size.y, size.z));
        body_shape_settings.mConvexRadius = 0.02;
        body_shape_settings.SetDensity(1000.0);
        JPH::ShapeSettings::ShapeResult body_shape_result = body_shape_settings.Create();
        shape = body_shape_result.Get();

        collider_ct::start();
    }

    void box_collider_ct::set_size(vec3_t size)
    {
        glm_vec3_copy(size.data, this->size.data);
    }

    JPH::Shape* box_collider_ct::get_shape() const
    {
        return shape.GetPtr();
    }
} // namespace smol::components
