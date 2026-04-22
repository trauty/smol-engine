#include "main_menu_bar.h"

#include "imgui.h"
#include "smol/log.h"
#include "smol/serialization.h"

#include "json/json.hpp"
#include <fstream>

namespace smol::editor::panels
{
    void draw_main_menu_bar(world_t& world, editor_context_t& ctx)
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save Scene"))
                {
                    nlohmann::json scene_json = smol::serialization::serialize_scene(world);
                    std::ofstream file("test_scene.json");
                    file << scene_json.dump(4);
                    SMOL_LOG_INFO("EDITOR", "Scene saved");
                }

                if (ImGui::MenuItem("Load Scene"))
                {
                    std::ifstream file("test_scene.json");
                    if (file.is_open())
                    {
                        nlohmann::json scene_json = nlohmann::json::parse(file);
                        smol::serialization::clear_scene(world);
                        smol::serialization::deserialize_scene(world, scene_json);
                        SMOL_LOG_INFO("EDITOR", "Scene loaded");
                    }
                }

                if (ImGui::MenuItem("Exit")) {}
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
    }
} // namespace smol::editor::panels