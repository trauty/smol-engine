#include "main_menu_bar.h"

#include "imgui.h"

namespace smol::editor::panels
{
    void draw_main_menu_bar(world_t& world, editor_context_t& ctx)
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save")) {}
                if (ImGui::MenuItem("Exit")) {}
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
    }
} // namespace smol::editor::panels