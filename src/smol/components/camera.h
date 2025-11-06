#pragma once

#include "smol/core/component.h"
#include "smol/defines.h"
#include "smol/events.h"
#include "smol/math_util.h"

#include <vector>

using namespace smol::math;

namespace smol::components
{
    class transform_ct;

    class SMOL_API camera_ct : public smol::core::component_t
    {
      public:
        static camera_ct* main_camera;
        static std::vector<camera_ct*> all_cameras;

        mat4_t view_matrix;
        mat4_t projection_matrix;
        transform_ct* transform;

        camera_ct(f32 fov_deg = 90.0f, f32 near_plane = 0.1f, f32 far_plane = 1000.0f, f32 aspect = 16.0f / 9.0f);
        ~camera_ct();

        void start();
        void update(f64 delta_time);

        void set_as_main_camera();

        // horizontal FOV
        void set_fov(f32 fov);

      private:
        f32 fov;
        f32 aspect;
        f32 near_plane;
        f32 far_plane;

        smol::events::subscription_id_t sub_id;
    };
} // namespace smol::components