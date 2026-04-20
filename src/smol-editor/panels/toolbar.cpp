#include "toolbar.h"

#include "imgui.h"
#include "smol-editor/systems/camera.h"
#include "smol/components/camera.h"
#include "smol/components/transform.h"
#include "smol/ecs_fwd.h"

namespace smol::editor::panels
{
    void draw_toolbar(world_t& world, editor_context_t& ctx)
    {
        ImGui::Begin("Toolbar");

        if (ctx.cur_mode == editor_mode_e::EDIT)
        {
            if (ImGui::Button("Play"))
            {
                if (world.registry.valid(ctx.editor_camera))
                {
                    world.registry.remove<smol::active_camera_tag>(ctx.editor_camera);
                }

                if (ctx.game_camera != smol::ecs::NULL_ENTITY && world.registry.valid(ctx.game_camera))
                {
                    world.registry.emplace_or_replace<smol::active_camera_tag>(ctx.game_camera);
                }

                smol::transform_t& transform = world.registry.get<smol::transform_t>(ctx.editor_camera);
                ctx.editor_cam_pos = transform.local_position;
                ctx.editor_cam_rot = transform.local_rotation;

                ctx.cur_mode = editor_mode_e::PLAY;
            }
        }
        else if (ctx.cur_mode == editor_mode_e::PLAY)
        {
            if (ImGui::Button("Stop"))
            {
                if (ctx.game_shutdown) { ctx.game_shutdown(&world); }
                world.registry.clear();
                if (ctx.game_init) { ctx.game_init(&world); }

                ctx.editor_camera = world.registry.create();
                smol::transform_t& transform = world.registry.emplace<smol::transform_t>(ctx.editor_camera);
                world.registry.emplace<smol::camera_t>(ctx.editor_camera);
                world.registry.emplace<smol::editor::editor_camera_tag>(ctx.editor_camera);
                world.registry.emplace<smol::active_camera_tag>(ctx.editor_camera);

                transform.local_position = ctx.editor_cam_pos;
                transform.local_rotation = ctx.editor_cam_rot;
                transform.is_dirty = true;

                ctx.selected_entity = smol::ecs::NULL_ENTITY;

                ctx.cur_mode = editor_mode_e::EDIT;
            }
        }

        ImGui::End();
    }
} // namespace smol::editor::panels