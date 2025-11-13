#include "camera.h"

#include "smol/core/gameobject.h"
#include "smol/log.h"
#include "smol/rendering/renderer.h"
#include "smol/window.h"
#include "transform.h"

#include <algorithm>

namespace smol::components
{
    camera_ct* camera_ct::main_camera = nullptr;
    std::vector<camera_ct*> camera_ct::all_cameras;

    camera_ct::camera_ct(
        f32 fov_deg,
        f32 near_plane,
        f32 far_plane,
        f32 aspect) : aspect(aspect),
                      near_plane(near_plane),
                      far_plane(far_plane)
    {
        set_fov(fov_deg);
    }

    camera_ct::~camera_ct()
    {
        std::vector<camera_ct*>::const_iterator it = std::find(all_cameras.begin(), all_cameras.end(), this);
        if (it != all_cameras.end())
        {
            all_cameras.erase(it);
            SMOL_LOG_DEBUG("CAMERA", "deleted cam");
        }

        if (main_camera == this) { main_camera = nullptr; }
    }

    void camera_ct::start()
    {
        transform = get_gameobject()->get_transform();
        all_cameras.push_back(this);

        sub_id = smol::events::subscribe<smol::window::size_changed_event_t>(
            [this](const smol::window::size_changed_event_t& ctx)
            {
                aspect = (f32)ctx.width / (f32)ctx.height;
            });
    }

    void camera_ct::update([[maybe_unused]] f64 delta_time)
    {
        // 11.09.2025: thank Jesus, i finally stopped being blind, rotations of camera are now correct
        glm_perspective_lh_no(fov, aspect, near_plane, far_plane, projection_matrix.data);

        mat4_t& cam_world = transform->get_world_matrix();
        glm_mat4_inv(cam_world.data, view_matrix.data);
    }

    void camera_ct::set_as_main_camera()
    {
        main_camera = this;
        smol::renderer::rebind_camera_block_to_all_shaders();
    }

    void camera_ct::set_fov(f32 fov)
    {
        this->fov = 2.0f * atanf(tanf(glm_rad(fov) / 2.0f) / aspect);
    }
} // namespace smol::components