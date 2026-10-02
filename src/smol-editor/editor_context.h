#pragma once

#include "smol-editor/systems/camera.h"
#include "smol/ecs_fwd.h"
#include "smol/game.h"
#include "smol/math.h"
#include "smol/rendering/renderer_types.h"

#include <json/json.hpp>
#include <vector>

namespace smol
{
    struct world_t;

    enum class editor_mode_e
    {
        EDIT,
        PLAY,
        PAUSED,
    };

    struct editor_context_t;

    using panel_draw_func = void (*)(smol::world_t& world, editor_context_t& ctx);

    struct editor_context_t
    {
        editor_mode_e cur_mode = editor_mode_e::EDIT;
        nlohmann::json world_backup;

        smol::editor::editor_camera_t editor_camera;

        smol::ecs::entity_t selected_entity = smol::ecs::NULL_ENTITY;

        bool is_viewport_hovered = false;
        bool is_viewport_focused = false;

        u32_t viewport_width = 0;
        u32_t viewport_height = 0;

        smol::renderer::debug_view_e debug_view = smol::renderer::debug_view_e::OFF;

        bool post_processing = true;

        game_register_types_func game_register_types = nullptr;
        game_init_func game_init = nullptr;
        game_update_func game_update = nullptr;
        game_shutdown_func game_shutdown = nullptr;

        std::vector<panel_draw_func> custom_panels;

        std::string project_dir;
        std::string project_file;
        std::string pending_scene_path;
        std::string current_scene_path;
        bool pending_scene_load = false;
        bool pending_scene_save = false;

        bool project_loaded = false;

        bool recompile_requested = false;
        bool recompiling = false;
        bool cancel_build_requested = false; // the console's Cancel, while recompiling

        bool show_project_manager = false;
    };
} // namespace smol