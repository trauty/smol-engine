#include "viewport.h"

#include "imgui.h"
#include "smol/input.h"
#include "smol/rendering/renderer.h"

namespace smol::editor::panels
{
    void draw_viewport(world_t& world, editor_context_t& ctx)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
        if (ImGui::Begin("Scene Viewport"))
        {
            ctx.is_viewport_hovered = ImGui::IsWindowHovered();
            ctx.is_viewport_focused = ImGui::IsWindowFocused();

            ImVec2 viewport_size = ImGui::GetContentRegionAvail();
            ImVec2 viewport_pos = ImGui::GetCursorScreenPos();

            if (viewport_size.x > 0.0f && viewport_size.y > 0.0f)
            {
                ctx.viewport_width = (u32_t)viewport_size.x;
                ctx.viewport_height = (u32_t)viewport_size.y;
            }

            smol::input::set_viewport_offset(viewport_pos.x, viewport_pos.y);
            smol::input::set_viewport_size(viewport_size.x, viewport_size.y);

            u32_t tex_id = smol::renderer::get_target_texture_id("EditorViewport"_h);
            if (tex_id != smol::renderer::BINDLESS_NULL_HANDLE)
            {
                ImGui::Image((ImTextureID)(intptr_t)(tex_id), viewport_size);
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }
} // namespace smol::editor::panels