#include "camera.h"

#include "imgui.h"
#include "smol/input.h"
#include "smol/time.h"

namespace smol::editor::camera_system
{
    static bool is_moving = false;

    void update(editor_camera_t& cam, bool is_viewport_hovered)
    {
        if (is_viewport_hovered && smol::input::get_mouse_button(smol::input::mouse_button_e::Right))
        {
            is_moving = true;
            smol::input::set_mouse_relative_mode(true);
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
        }

        if (!smol::input::get_mouse_button(smol::input::mouse_button_e::Right))
        {
            is_moving = false;
            smol::input::set_mouse_relative_mode(false);
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        }

        if (!is_moving) { return; }

        vec3_t forward = cam.rotation.forward();
        vec3_t right = cam.rotation.right();

        f32 speed = 10.0f * smol::time::get_dt();
        if (smol::input::get_key(smol::input::key_e::LeftShift)) { speed *= 2.0f; }

        if (smol::input::get_key(smol::input::key_e::W)) { cam.position += forward * speed; }
        if (smol::input::get_key(smol::input::key_e::S)) { cam.position -= forward * speed; }
        if (smol::input::get_key(smol::input::key_e::D)) { cam.position += right * speed; }
        if (smol::input::get_key(smol::input::key_e::A)) { cam.position -= right * speed; }

        vec2_t mouse_delta = smol::input::get_mouse_delta();
        f32 mouse_sensitivity = 0.0015f;

        if (mouse_delta.x != 0.0f || mouse_delta.y != 0.0f)
        {
            cam.yaw += mouse_delta.x * mouse_sensitivity;
            cam.pitch -= mouse_delta.y * mouse_sensitivity;

            const f32 pitch_limit = smol::math::deg_to_rad(90.0f);
            if (cam.pitch > pitch_limit) { cam.pitch = pitch_limit; }
            if (cam.pitch < -pitch_limit) { cam.pitch = -pitch_limit; }

            quat_t q_yaw = quat_t::angle_axis(cam.yaw, vec3_t::up());
            quat_t q_pitch = quat_t::angle_axis(cam.pitch, vec3_t::right());

            cam.rotation = q_yaw * q_pitch;
        }
    }
}; // namespace smol::editor::camera_system
