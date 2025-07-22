#include "camera.h"
#include <algorithm>
#include "transform.h"
#include "core/gameobject.h"
#include "rendering/renderer.h"
#include "window.h"
#include "log.h"

#include <iostream>

namespace smol::components
{   
    camera_ct* camera_ct::main_camera = nullptr;
    std::vector<camera_ct*> camera_ct::all_cameras;

    camera_ct::camera_ct(
        f32 fov_deg, 
        f32 near_plane, 
        f32 far_plane, 
        f32 aspect
    ) : aspect(aspect), near_plane(near_plane), far_plane(far_plane) 
    {
        set_fov(fov_deg);
    }
    
    camera_ct::~camera_ct()
    {
        std::vector<camera_ct*>::const_iterator it = std::find(all_cameras.begin(), all_cameras.end(), this);
        if (it != all_cameras.end())
        {
            all_cameras.erase(it);
        }

        if (main_camera == this) main_camera = nullptr;
    }

    void camera_ct::start()
    {
        transform = get_gameobject()->get_transform();
        all_cameras.push_back(this);

        sub_id = smol::events::subscribe<smol::window::window_size_changed_event_t>(
            [this](const smol::window::window_size_changed_event_t& ctx) {
                aspect = (f32)ctx.width / (f32)ctx.height;
            }
        );
    }

    void camera_ct::update(f64 delta_time)
    {
        vec3_t pos = transform->get_world_position();
        vec3_t forward = transform->get_forward();
        vec3_t up = transform->get_up();
        vec3_t dir;

        glmc_vec3_add(pos.data, forward.data, dir.data);
        glmc_look(pos.data, dir.data, up.data, view_matrix.data);
        glmc_perspective(fov, aspect, near_plane, far_plane, projection_matrix.data);
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
}