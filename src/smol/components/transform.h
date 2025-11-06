#pragma once

#include "smol/core/component.h"
#include "smol/math_util.h"

using namespace smol::math;

namespace smol::core
{
    class gameobject_t;
}

namespace smol::components
{
    class SMOL_API transform_ct : public smol::core::component_t
    {
      public:
        transform_ct() = default;

        vec3_t local_position;
        quat_t local_rotation;
        vec3_t local_scale {1.0f, 1.0f, 1.0f};

        void set_local_position(vec3_t pos);
        void set_local_rotation(quat_t rot);
        void set_local_scale(vec3_t scale);

        vec3_t get_local_euler_angles();

        vec3_t get_world_position();
        quat_t get_world_rotation();
        vec3_t get_world_scale();
        vec3_t get_world_euler_angles();

        vec3_t get_forward();
        vec3_t get_right();
        vec3_t get_up();

        void set_world_position(vec3_t pos);
        void set_world_rotation(quat_t rot);
        void set_world_scale(vec3_t scale);
        void set_world_euler_angles(vec3_t angles);

        void translate_local(vec3_t val);
        void rotate_local(vec3_t euler);
        void scale_local(vec3_t scale);

        mat4_t& get_local_matrix();
        mat4_t& get_world_matrix();

        transform_ct* get_parent_transform();

      private:
        mutable mat4_t cached_world_matrix, cached_local_matrix;
        mutable bool world_dirty = true, local_dirty = true;

        void mark_local_dirty();
        void mark_world_dirty();
        void update_local_matrix();
        void update_world_matrix();

        inline float wrap_three_sixty(f32 angle);
        inline f32 delta_angle(f32 current, f32 target);
    };
} // namespace smol::components