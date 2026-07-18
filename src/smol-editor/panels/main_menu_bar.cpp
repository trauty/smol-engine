#include "main_menu_bar.h"

#include "imgui.h"
#include "smol/log.h"
#include "smol/serialization.h"
#include "smol/window.h"

#include <SDL3/SDL_dialog.h>
#include <fstream>
#include <json/json.hpp>

namespace smol::editor::panels
{
    static void SDLCALL open_scene_callback(void* userdata, const char* const* filelist, int)
    {
        auto* ctx = static_cast<smol::editor_context_t*>(userdata);
        if (filelist && filelist[0])
        {
            ctx->pending_scene_path = filelist[0];
            ctx->pending_scene_load = true;
        }
    }

    static void SDLCALL save_scene_callback(void* userdata, const char* const* filelist, int)
    {
        auto* ctx = static_cast<smol::editor_context_t*>(userdata);
        if (filelist && filelist[0])
        {
            ctx->pending_scene_path = filelist[0];
            ctx->pending_scene_save = true;
        }
    }

    void draw_main_menu_bar(world_t& world, editor_context_t& ctx)
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open Project Manager")) { ctx.show_project_manager = true; }

                ImGui::Separator();

                if (ImGui::MenuItem("New Scene"))
                {
                    smol::serialization::clear_scene(world);
                    ctx.current_scene_path.clear();
                    SMOL_LOG_INFO("EDITOR", "New scene created");
                }

                if (ImGui::MenuItem("Open Scene..."))
                {
                    SDL_DialogFileFilter filters[] = {
                        {"Smol Scene", "scene"}
                    };
                    SDL_ShowOpenFileDialog(open_scene_callback, &ctx, smol::window::get_window(), filters, 1,
                                           ctx.project_dir.empty() ? nullptr : ctx.project_dir.c_str(), false);
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Save", nullptr, false, !ctx.current_scene_path.empty()))
                {
                    nlohmann::json scene_json = smol::serialization::serialize_scene(world);
                    std::ofstream file(ctx.current_scene_path);
                    file << scene_json.dump(4);
                    SMOL_LOG_INFO("EDITOR", "Scene saved to {}", ctx.current_scene_path);
                }

                if (ImGui::MenuItem("Save As..."))
                {
                    SDL_DialogFileFilter filters[] = {
                        {"Smol Scene", "scene"}
                    };
                    SDL_ShowSaveFileDialog(save_scene_callback, &ctx, smol::window::get_window(), filters, 1,
                                           ctx.project_dir.empty() ? nullptr : ctx.project_dir.c_str());
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Exit")) {}
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
    }
} // namespace smol::editor::panels