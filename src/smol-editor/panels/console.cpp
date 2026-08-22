#include "console.h"

#include "imgui.h"
#include "imgui_internal.h"

namespace smol::editor::panels
{
    void draw_console(world_t& world, editor_context_t& ctx)
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 32.0f), ImVec2(FLT_MAX, 32.0f));

        ImGuiWindowClass window_class;
        window_class.DockNodeFlagsOverrideSet =
            ImGuiDockNodeFlags_NoResize | (ImGuiDockNodeFlags)ImGuiDockNodeFlags_NoTabBar;

        ImGui::SetNextWindowClass(&window_class);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
        ImGui::Begin("Console", nullptr, flags);

        ImGui::BeginDisabled(ctx.recompiling);
        if (ImGui::Button("Recompile")) { ctx.recompile_requested = true; }
        ImGui::EndDisabled();

        if (ctx.recompiling)
        {
            ImGui::SameLine();
            ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(160.0f, 0.0f), "building...");
        }

        ImGui::End();
    }
} // namespace smol::editor::panels
