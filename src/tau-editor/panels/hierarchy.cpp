#include "hierarchy.h"

#include "imgui.h"
#include "tau/components/tag.h"
#include "tau/components/transform.h"
#include "tau/ecs_fwd.h"

namespace tau::editor::panels
{
    void draw_hierarchy(world_t& world, editor_context_t& ctx)
    {
        if (ImGui::Begin("Hierarchy"))
        {
            if (ImGui::Button("+ Create Entity"))
            {
                tau::ecs::entity_t entity = world.registry.create();
                world.registry.emplace<tau::tag_t>(entity, "New Entity");
                world.registry.emplace<tau::transform_t>(entity);
                ctx.selected_entity = entity;
            }

            ImGui::Separator();

            for (tau::ecs::entity_t entity : world.registry.view<tau::tag_t>())
            {
                ImGui::PushID(static_cast<i32>(tau::ecs::get_entity_id(entity)));

                std::string entity_name = world.registry.get<tau::tag_t>(entity).name;

                bool is_selected = (ctx.selected_entity == entity);
                if (ImGui::Selectable(entity_name.c_str(), is_selected)) { ctx.selected_entity = entity; }

                if (ImGui::BeginPopupContextItem("Delete Entity"))
                {
                    if (ImGui::MenuItem("Delete"))
                    {
                        if (ctx.selected_entity == entity) { ctx.selected_entity = tau::ecs::NULL_ENTITY; }
                        world.registry.destroy(entity);
                    }

                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }

        ImGui::End();
    }
} // namespace tau::editor::panels