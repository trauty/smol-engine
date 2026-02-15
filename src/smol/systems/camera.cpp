#include "camera.h"
#include "cglm/cam.h"
#include "cglm/clipspace/persp_lh_zo.h"
#include "cglm/clipspace/view_lh.h"
#include "cglm/util.h"
#include "cglm/vec3.h"
#include "smol/components/camera.h"
#include "smol/components/transform.h"
#include "smol/ecs.h"
#include "smol/math.h"
#include "smol/window.h"
#include <tuple>

namespace smol::camera_system
{
    void set_fov(ecs::registry_t& reg, ecs::entity_t entity, f32 fov_deg)
    {
        if (reg.has<camera_t>(entity))
        {
            camera_t& cam = reg.get<camera_t>(entity);
            cam.fov_deg = fov_deg;
            cam.is_dirty = true;
        }
    }

    ecs::entity_t get_active_camera(ecs::registry_t& reg)
    {
        auto view = reg.view<active_camera_tag>();
        if (view.begin() == view.end()) { return ecs::NULL_ENTITY; }

        auto [entity, tag] = *view.begin();
        return entity;
    }

    void update(ecs::registry_t& reg)
    {
        for (auto [entity, event] : reg.view<window::window_size_changed_event>())
        {
            f32 new_aspect = static_cast<f32>(event.width) / static_cast<f32>(event.height);

            for (auto [entity, cam] : reg.view<camera_t>())
            {
                cam.aspect = new_aspect;
                cam.is_dirty = true;
            }
        }

        for (auto [entity, cam, transform] : reg.view<camera_t, transform_t>())
        {
            if (cam.is_dirty)
            {
                glm_perspective_lh_zo(glm_rad(cam.fov_deg), cam.aspect, cam.near_plane, cam.far_plane,
                                      cam.projection.data);

                cam.projection.data[1][1] *= -1.0f;

                cam.is_dirty = false;
            }

            vec3_t eye = {transform.world_mat[3][0], transform.world_mat[3][1], transform.world_mat[3][2]};
            vec3_t forward = {transform.world_mat[2][0], transform.world_mat[2][1], transform.world_mat[2][2]};
            vec3_t up = {transform.world_mat[1][0], transform.world_mat[1][1], transform.world_mat[1][2]};

            vec3_t center;
            glm_vec3_add(eye.data, forward.data, center.data);
            glm_lookat_lh(eye.data, center.data, up.data, cam.view.data);
            glm_mat4_mul(cam.projection.data, cam.view.data, cam.view_proj.data);
        }
    }
} // namespace smol::camera_system