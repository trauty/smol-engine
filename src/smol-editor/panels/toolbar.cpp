#include "toolbar.h"

#include "imgui.h"
#include "smol/ecs_fwd.h"

namespace smol::editor::panels
{
    void draw_toolbar(world_t& world, editor_context_t& ctx)
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 32.0f), ImVec2(FLT_MAX, 32.0f));

        ImGuiWindowClass window_class;
        window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoResize;
        ImGui::SetNextWindowClass(&window_class);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
        ImGui::Begin("Toolbar", nullptr, flags);

        if (ctx.cur_mode == editor_mode_e::EDIT)
        {
            if (ImGui::Button("Play")) { ctx.cur_mode = editor_mode_e::PLAY; }
        }
        else if (ctx.cur_mode == editor_mode_e::PLAY)
        {
            if (ImGui::Button("Stop"))
            {
                ctx.selected_entity = smol::ecs::NULL_ENTITY;
                ctx.cur_mode = editor_mode_e::EDIT;
            }
        }

        ImGui::End();
    }
} // namespace smol::editor::panels
