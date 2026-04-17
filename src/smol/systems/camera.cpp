#include "camera.h"

#include "cglm/clipspace/persp_lh_zo.h"
#include "cglm/clipspace/view_lh.h"
#include "cglm/util.h"
#include "cglm/vec3.h"
#include "smol/components/camera.h"
#include "smol/components/transform.h"
#include "smol/math.h"
#include "smol/rendering/renderer.h"

namespace smol::camera_system
{
    void set_fov(ecs::registry_t& reg, ecs::entity_t entity, f32 fov_deg)
    {
        if (camera_t* cam = reg.try_get<camera_t>(entity))
        {
            cam->fov_deg = fov_deg;
            cam->is_dirty = true;
        }
    }

    ecs::entity_t get_active_camera(ecs::registry_t& reg)
    {
        auto view = reg.view<active_camera_tag>();
        return view.empty() ? ecs::NULL_ENTITY : view.front();
    }

    void update(ecs::registry_t& reg)
    {
        u32_t cur_width = smol::renderer::ctx.logical_extent.width;
        u32_t cur_height = smol::renderer::ctx.logical_extent.height;

        if (cur_width == 0 || cur_height == 0) { return; }

        f32 cur_aspect = static_cast<f32>(cur_width) / static_cast<f32>(cur_height);

        for (auto [entity, cam, transform] : reg.view<camera_t, transform_t>().each())
        {
            if (std::abs(cam.aspect - cur_aspect) > 0.001f)
            {
                cam.aspect = cur_aspect;
                cam.is_dirty = true;
            }

            if (cam.is_dirty)
            {
                glm_perspective_lh_zo(glm_rad(cam.fov_deg), cam.aspect, cam.near_plane, cam.far_plane, cam.projection);
                cam.is_dirty = false;
            }

            vec3_t eye = {transform.world_mat[3][0], transform.world_mat[3][1], transform.world_mat[3][2]};
            vec3_t forward = {transform.world_mat[2][0], transform.world_mat[2][1], transform.world_mat[2][2]};
            vec3_t up = {transform.world_mat[1][0], transform.world_mat[1][1], transform.world_mat[1][2]};

            vec3_t center;
            glm_vec3_add(eye, forward, center);
            glm_lookat_lh(eye, center, up, cam.view);
            glm_mat4_mul(cam.projection, cam.view, cam.view_proj);
        }
    }
} // namespace smol::camera_system