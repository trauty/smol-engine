#include "hierarchy.h"

#include "imgui.h"
#include "smol-editor/systems/camera.h"
#include "smol/components/tag.h"
#include "smol/components/transform.h"
#include "smol/ecs_fwd.h"

namespace smol::editor::panels
{
    void draw_hierarchy(world_t& world, editor_context_t& ctx)
    {
        if (ImGui::Begin("Hierarchy"))
        {
            if (ImGui::Button("+ Create Entity"))
            {
                smol::ecs::entity_t entity = world.registry.create();
                world.registry.emplace<smol::tag_t>(entity, "New Entity");
                world.registry.emplace<smol::transform_t>(entity);
                ctx.selected_entity = entity;
            }

            ImGui::Separator();

            for (smol::ecs::entity_t entity : world.registry.view<smol::tag_t>())
            {
                if (world.registry.all_of<smol::editor::editor_camera_tag>(entity)) { continue; }

                ImGui::PushID(static_cast<i32>(smol::ecs::get_entity_id(entity)));

                std::string entity_name = world.registry.get<smol::tag_t>(entity).name;

                bool is_selected = (ctx.selected_entity == entity);
                if (ImGui::Selectable(entity_name.c_str(), is_selected)) { ctx.selected_entity = entity; }

                if (ImGui::BeginPopupContextItem("Delete Entity"))
                {
                    if (ImGui::MenuItem("Delete"))
                    {
                        if (ctx.selected_entity == entity) { ctx.selected_entity = smol::ecs::NULL_ENTITY; }
                        world.registry.destroy(entity);
                    }

                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }

        ImGui::End();
    }
} // namespace smol::editor::panels