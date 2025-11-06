#include "transform.h"

#include "smol/core/gameobject.h"
#include "smol/log.h"

namespace smol::components
{
    void transform_ct::set_local_position(vec3_t pos)
    {
        glm_vec3_copy(pos.data, local_position.data);
        mark_local_dirty();
    }

    void transform_ct::set_local_rotation(quat_t rot)
    {
        glm_quat_copy(rot.data, local_rotation.data);
        mark_local_dirty();
    }

    void transform_ct::set_local_scale(vec3_t scale)
    {
        glm_vec3_copy(scale.data, local_scale.data);
        mark_local_dirty();
    }

    vec3_t transform_ct::get_local_euler_angles()
    {
        vec3 angles;
        glm_euler_angles(cached_local_matrix.data, angles);
        return vec3_t {
            wrap_three_sixty(glm_deg(angles[0])),
            wrap_three_sixty(glm_deg(angles[1])),
            wrap_three_sixty(glm_deg(angles[2]))};
    }

    vec3_t transform_ct::get_world_position()
    {
        mat4_t& wm = get_world_matrix();
        vec3_t p;
        glm_mat4_mulv3(wm.data, vec3_t {0.0f, 0.0f, 0.0f}.data, 1.0f, p.data);
        return p;
    }

    quat_t transform_ct::get_world_rotation()
    {
        mat4_t& wm = get_world_matrix();
        mat4 rot_mat;
        vec3 scale;
        glm_decompose_rs(wm.data, rot_mat, scale);
        quat_t q;
        glm_mat4_quat(rot_mat, q.data);
        return q;
    }

    vec3_t transform_ct::get_world_scale()
    {
        mat4_t& wm = get_world_matrix();
        vec3 scale;
        glm_decompose_scalev(wm.data, scale);
        return vec3_t {scale};
    }

    vec3_t transform_ct::get_world_euler_angles()
    {
        return vec3_t {};
    }

    vec3_t transform_ct::get_forward()
    {
        mat4_t& wm = get_world_matrix();
        vec3_t forward = vec3_t {wm.m02, wm.m12, wm.m22};
        glm_vec3_normalize(forward.data);
        return forward;
    }

    vec3_t transform_ct::get_right()
    {
        mat4_t& wm = get_world_matrix();
        vec3_t right = vec3_t {wm.m00, wm.m10, wm.m20};
        glm_vec3_normalize(right.data);
        return right;
    }

    vec3_t transform_ct::get_up()
    {
        mat4_t& wm = get_world_matrix();
        vec3_t up = vec3_t {wm.m01, wm.m11, wm.m21};
        glm_vec3_normalize(up.data);
        return up;
    }

    mat4_t& transform_ct::get_local_matrix()
    {
        if (local_dirty)
            update_local_matrix();
        return cached_local_matrix;
    }

    mat4_t& transform_ct::get_world_matrix()
    {
        if (world_dirty)
            update_world_matrix();
        return cached_world_matrix;
    }

    void transform_ct::set_world_position(vec3_t pos)
    {
        if (transform_ct* parent_transform = get_parent_transform())
        {
            mat4_t& wm_inv = parent_transform->get_world_matrix();
            glm_mat4_inv(wm_inv.data, wm_inv.data);
            glm_mat4_mulv3(wm_inv.data, pos.data, 1.0f, local_position.data);
        }
        else
        {
            glm_vec3_copy(pos.data, local_position.data);
        }

        mark_local_dirty();
    }

    void transform_ct::set_world_rotation(quat_t rot)
    {
        if (transform_ct* parent_transform = get_parent_transform())
        {
            quat_t rot_inv = parent_transform->get_world_rotation();
            glm_quat_inv(rot_inv.data, rot_inv.data);
            glm_quat_mul(rot_inv.data, rot.data, local_rotation.data);
        }
        else
        {
            glm_quat_copy(rot.data, local_rotation.data);
        }

        glm_quat_normalize(local_rotation.data);

        mark_local_dirty();
    }

    void transform_ct::set_world_scale(vec3_t scale)
    {
        if (transform_ct* parent_transform = get_parent_transform())
        {
            vec3_t parent_scale = parent_transform->get_world_scale();
            glm_vec3_div(scale.data, parent_scale.data, local_scale.data);
        }
        else
        {
            glm_vec3_copy(scale.data, local_scale.data);
        }

        mark_local_dirty();
    }

    void transform_ct::set_world_euler_angles(vec3_t angles)
    {
        vec3_t angles_rad {glm_rad(angles.x), glm_rad(angles.y), glm_rad(angles.z)};
        quat_t quat;
        glm_euler_xyz_quat(angles_rad.data, quat.data);
        set_world_rotation(quat);
    }

    void transform_ct::translate_local(vec3_t val)
    {
        glm_vec3_add(local_position.data, val.data, local_position.data);
        mark_local_dirty();
    }

    void transform_ct::rotate_local(vec3_t euler)
    {
        vec3 r = {glm_rad(euler.x), glm_rad(euler.y), glm_rad(euler.z)};
        vec4 dq;
        glm_euler_zxy_quat(r, dq);
        glm_quat_mul(dq, local_rotation.data, local_rotation.data);
        glm_quat_normalize(local_rotation.data);
        mark_local_dirty();
    }

    void transform_ct::scale_local(vec3_t scale)
    {
        glm_vec3_mul(local_scale.data, scale.data, local_scale.data);
        mark_local_dirty();
    }

    transform_ct* transform_ct::get_parent_transform()
    {
        if (std::shared_ptr<smol::core::gameobject_t> crt_owner = get_gameobject())
        {
            return crt_owner->get_parent() ? crt_owner->get_parent()->get_transform() : nullptr;
        }

        return nullptr;
    }

    void transform_ct::mark_local_dirty()
    {
        local_dirty = true;
        mark_world_dirty();
    }

    void transform_ct::mark_world_dirty()
    {
        if (world_dirty)
            return;

        world_dirty = true;

        if (const std::shared_ptr<smol::core::gameobject_t> parent = get_gameobject())
        {
            for (const std::shared_ptr<smol::core::gameobject_t>& child : parent->get_children())
            {
                child->get_transform()->mark_world_dirty();
            }
        }
    }

    void transform_ct::update_local_matrix()
    {
        mat4_t t, r, s, trs;
        glm_translate_make(t.data, local_position.data);
        glm_quat_mat4(local_rotation.data, r.data);
        glm_scale_make(s.data, local_scale.data);

        glm_mat4_mul(r.data, s.data, trs.data);
        glm_mat4_mul(t.data, trs.data, cached_local_matrix.data);

        local_dirty = false;
    }

    void transform_ct::update_world_matrix()
    {
        mat4_t& crt_local_matrix = get_local_matrix();
        if (transform_ct* parent_transform = get_parent_transform())
        {
            glm_mat4_mul(
                parent_transform->get_world_matrix().data,
                crt_local_matrix.data,
                cached_world_matrix.data);
        }
        else
        {
            glm_mat4_copy(cached_local_matrix.data, cached_world_matrix.data);
        }

        world_dirty = false;
    }

    inline f32 transform_ct::wrap_three_sixty(f32 angle)
    {
        angle = std::fmodf(angle, 360.0f);
        if (angle < 0.0f)
            angle += 360.0f;
        return angle;
    }

    inline f32 transform_ct::delta_angle(f32 current, f32 target)
    {
        return std::fmodf(target - current + 540.0f, 360.0f) - 180.0f;
    }
} // namespace smol::components