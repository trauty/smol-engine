#include "shadows.h"

#include "cglm/clipspace/ortho_lh_zo.h"
#include "cglm/clipspace/view_lh.h"
#include "cglm/vec3.h"
#include "smol/components/lighting.h"
#include "smol/components/transform.h"
#include "smol/hash.h"
#include "smol/math.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_types.h"

namespace smol::shadow_system
{
    static constexpr f32 SHADOW_ORTHO_HALF_EXTENT = 40.0f; // world units covered on each axis
    static constexpr f32 SHADOW_DISTANCE = 50.0f;          // how far back the light view sits
    static constexpr f32 SHADOW_NEAR = 0.1f;
    static constexpr f32 SHADOW_FAR = 100.0f;
    static constexpr u32_t SHADOW_MAP_DIM = 2048;

    void update(ecs::registry_t& reg)
    {
        vec3_t cam_pos;
        if (!renderer::get_primary_camera_position(reg, cam_pos)) { return; }

        auto dir_lights = reg.view<directional_light_t, transform_t>();
        if (dir_lights.begin() == dir_lights.end()) { return; }

        ecs::entity_t light_entity = dir_lights.front();
        transform_t& light_transform = reg.get<transform_t>(light_entity);

        vec3_t dir;
        glm_vec3_normalize_to(
            (vec3){light_transform.world_mat[2][0], light_transform.world_mat[2][1], light_transform.world_mat[2][2]},
            dir);
        vec3_t up;
        glm_vec3_normalize_to(
            (vec3){light_transform.world_mat[1][0], light_transform.world_mat[1][1], light_transform.world_mat[1][2]},
            up);

        vec3_t center = {cam_pos.x, cam_pos.y, cam_pos.z};
        vec3_t eye = {center.x - dir.x * SHADOW_DISTANCE, center.y - dir.y * SHADOW_DISTANCE,
                      center.z - dir.z * SHADOW_DISTANCE};

        mat4_t view;
        glm_lookat_lh(eye, center, up, view);

        mat4_t projection;
        glm_ortho_lh_zo(-SHADOW_ORTHO_HALF_EXTENT, SHADOW_ORTHO_HALF_EXTENT, -SHADOW_ORTHO_HALF_EXTENT,
                        SHADOW_ORTHO_HALF_EXTENT, SHADOW_NEAR, SHADOW_FAR, projection);
        flip_clip_y(projection);

        renderer::image_desc_t shadow_desc{};
        shadow_desc.width = SHADOW_MAP_DIM;
        shadow_desc.height = SHADOW_MAP_DIM;
        shadow_desc.format = renderer::ctx.swapchain.depth_format;
        shadow_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        shadow_desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;

        renderer::submit_shadow_view("DirShadowView"_h, view, projection, "ShadowMap"_h,
                                     {SHADOW_MAP_DIM, SHADOW_MAP_DIM}, shadow_desc);
    }
} // namespace smol::shadow_system
