#include "camera.h"

#include "imgui.h"
#include "smol/components/camera.h"
#include "smol/components/transform.h"
#include "smol/ecs.h"
#include "smol/input.h"
#include "smol/log.h"
#include "smol/math.h"
#include "smol/time.h"

namespace smol::editor::camera_system
{
    static bool is_moving = false;

    static f32 pitch = 0.0f;
    static f32 yaw = 0.0f;

    void update(ecs::registry_t& reg, bool is_viewport_hovered)
    {
        auto view = reg.view<editor_camera_tag, transform_t, camera_t>();

        if (is_viewport_hovered && smol::input::get_mouse_button(smol::input::mouse_button_t::Right))
        {
            is_moving = true;
            smol::input::set_mouse_relative_mode(true);
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
        }

        if (!smol::input::get_mouse_button(smol::input::mouse_button_t::Right))
        {
            is_moving = false;
            smol::input::set_mouse_relative_mode(false);
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        }

        for (ecs::entity_t entity : view)
        {
            transform_t& transform = view.get<transform_t>(entity);

            if (is_moving)
            {
                vec3_t forward = transform.local_rotation.forward();
                vec3_t right = transform.local_rotation.right();

                f32 speed = 10.0f * smol::time::get_dt();
                if (smol::input::get_key(smol::input::key_t::LeftShift)) { speed *= 2.0f; }

                if (smol::input::get_key(smol::input::key_t::W)) { transform.local_position += forward * speed; }
                if (smol::input::get_key(smol::input::key_t::S)) { transform.local_position -= forward * speed; }
                if (smol::input::get_key(smol::input::key_t::D)) { transform.local_position += right * speed; }
                if (smol::input::get_key(smol::input::key_t::A)) { transform.local_position -= right * speed; }

                vec2_t mouse_delta = smol::input::get_mouse_delta();
                f32 mouse_sensitity = 0.0015f;

                if (mouse_delta.x != 0.0f || mouse_delta.y != 0.0f)
                {
                    yaw += mouse_delta.x * mouse_sensitity;
                    pitch -= mouse_delta.y * mouse_sensitity;

                    const f32 pitch_limit = smol::math::deg_to_rad(90.0f);
                    if (pitch > pitch_limit) { pitch = pitch_limit; }
                    if (pitch < -pitch_limit) { pitch = -pitch_limit; }

                    quat_t q_yaw = quat_t::angle_axis(yaw, vec3_t::up());
                    quat_t q_pitch = quat_t::angle_axis(pitch, vec3_t::right());

                    transform.local_rotation = q_yaw * q_pitch;
                }

                transform.is_dirty = true;
            }
        }
    }
}; // namespace smol::editor::camera_system