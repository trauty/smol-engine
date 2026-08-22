#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui_backend.h"
#include "imgui_internal.h"
#include "smol-editor/editor_context.h"
#include "smol-editor/panels/console.h"
#include "smol-editor/panels/hierarchy.h"
#include "smol-editor/panels/inspector.h"
#include "smol-editor/panels/main_menu_bar.h"
#include "smol-editor/panels/toolbar.h"
#include "smol-editor/panels/viewport.h"
#include "smol-editor/project_manager.h"
#include "smol-editor/systems/camera.h"
#include "smol/asset_meta.h"
#include "smol/engine.h"
#include "smol/game.h"
#include "smol/hash.h"
#include "smol/log.h"
#include "smol/os.h"
#include "smol/project.h"
#include "smol/reflection.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/vulkan.h"
#include "smol/serialization.h"
#include "smol/systems/camera.h"
#include "smol/tween.h"
#include "smol/vfs.h"
#include "smol/window.h"
#include "smol/world.h"

#include "json/json.hpp"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_stdinc.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <system_error>
#include <unordered_set>

#if SMOL_PLATFORM_WIN
    #include <windows.h>
#elif SMOL_PLATFORM_LINUX
    #include <dlfcn.h>
#endif

std::filesystem::path source_lib_path;
std::filesystem::path trigger_path;
std::string source_lib_name;
std::string cur_temp_lib_name = "";

smol::os::lib_handle_t game_lib = nullptr;
std::filesystem::file_time_type last_reload_time;

std::unordered_set<u32_t> g_engine_pool_ids;

typedef void (*editor_init_func)(smol::world_t*, smol::editor_context_t*, ImGuiContext*);

bool build_project(const std::filesystem::path& project_file, const std::string& engine_dir)
{
    smol::project_t project;
    if (!smol::project_t::load(project_file, project))
    {
        SMOL_LOG_ERROR("EDITOR", "Failed to load project for build: {}", project_file.string());
        return false;
    }
    const std::string proj = project.project_dir.string();
    const std::string target = project.project_name;

#ifndef SMOL_EDITOR_MODE
    #define SMOL_EDITOR_MODE "debug"
#endif
    const std::string mode = SMOL_EDITOR_MODE;

    if (!engine_dir.empty())
    {
        // The project's xmake.lua discovers the engine itself, but we know exactly which one
        // is running this editor, so name it. SMOL_ENGINE_DIR is the first thing it checks,
        // and std::system's child inherits our environment.
        SDL_setenv_unsafe("SMOL_ENGINE_DIR", engine_dir.c_str(), 1);

        const std::string cfg = "xmake f -y -m " + mode + " -P \"" + proj + "\"";
        SMOL_LOG_INFO("EDITOR", "Configuring project: {}", cfg);
        if (std::system(cfg.c_str()) != 0)
        {
            SMOL_LOG_ERROR("EDITOR", "xmake configure failed");
            return false;
        }
    }

    const std::string build = "xmake -P \"" + proj + "\" " + target;
    SMOL_LOG_INFO("EDITOR", "Building project: {}", build);
    if (std::system(build.c_str()) != 0)
    {
        SMOL_LOG_ERROR("EDITOR", "xmake build failed");
        return false;
    }
    SMOL_LOG_INFO("EDITOR", "Project built successfully");
    return true;
}

bool load_game_dll(bool is_reload, smol::editor_context_t& ctx)
{
    smol::serialization::reload_snapshot_t reload_snapshot;

    if (game_lib)
    {
        smol::world_t& world = smol::engine::get_active_world();

        smol::tween::cancel_all();

        reload_snapshot = smol::serialization::evict_game_components(world, g_engine_pool_ids);

        smol::reflection::shutdown();

        smol::reflection::clear_registrations();

        smol::os::free_lib(game_lib);
        game_lib = nullptr;
    }

    if (!cur_temp_lib_name.empty() && std::filesystem::exists(cur_temp_lib_name))
    {
        std::error_code ec;
        std::filesystem::remove(cur_temp_lib_name, ec);

        if (ec) { SMOL_LOG_WARN("EDITOR", "Could not delete old temp lib: {}", ec.message()); }
    }

    static int reload_counter = 0;
    std::filesystem::path temp_lib_path =
        source_lib_path.parent_path() / (source_lib_path.stem().string() + "-loaded-" +
                                         std::to_string(reload_counter++) + source_lib_path.extension().string());

    cur_temp_lib_name = temp_lib_path.string();

    std::error_code ec;
    std::filesystem::copy_file(source_lib_name, cur_temp_lib_name, std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec)
    {
        SMOL_LOG_WARN("EDITOR", "Could not copy game logic lib '{}': {}", source_lib_name, ec.message());
        return false;
    }

    game_lib = smol::os::load_lib(cur_temp_lib_name.c_str());
    if (!game_lib)
    {
#if SMOL_PLATFORM_WIN
        SMOL_LOG_FATAL("ENGINE", "Failed to load library with name: {}; Error: {}", cur_temp_lib_name, GetLastError());
#elif SMOL_PLATFORM_LINUX
        SMOL_LOG_FATAL("ENGINE", "Failed to load library with name: {}; Error: {}", cur_temp_lib_name, dlerror());
#endif
        return false;
    }

    ctx.game_register_types =
        (game_register_types_func)smol::os::get_proc_address(game_lib, "smol_game_register_types_internal");
    ctx.game_init = (game_init_func)smol::os::get_proc_address(game_lib, "smol_game_init_internal");
    ctx.game_update = (game_update_func)smol::os::get_proc_address(game_lib, "smol_game_update_internal");
    ctx.game_shutdown = (game_shutdown_func)smol::os::get_proc_address(game_lib, "smol_game_shutdown_internal");

    editor_init_func editor_init = (editor_init_func)smol::os::get_proc_address(game_lib, "smol_editor_init");

    if (!ctx.game_register_types || !ctx.game_init || !ctx.game_update || !ctx.game_shutdown)
    {
        SMOL_LOG_ERROR("EDITOR", "Could not find one or more game logic functions necessary");
        return false;
    }

    std::error_code trigger_ec;
    last_reload_time = std::filesystem::last_write_time(trigger_path, trigger_ec);

    smol::world_t& world = smol::engine::get_active_world();
    smol::reflection::register_types();

    if (g_engine_pool_ids.empty()) { g_engine_pool_ids = smol::serialization::reflected_component_pool_ids(world); }

    ctx.game_register_types(&world);

    if (is_reload) { smol::serialization::restore_game_components(world, reload_snapshot); }
    else
    {
        ctx.game_init(&world);
    }

    if (editor_init)
    {
        ctx.custom_panels.clear();
        editor_init(&smol::engine::get_active_world(), &ctx, ImGui::GetCurrentContext());
    }

    SMOL_LOG_INFO("EDITOR", "{}", is_reload ? "Successfully hot-reloaded game lib" : "Successfully loaded game lib");
    return true;
}

bool process_editor_event(const SDL_Event& event, smol::editor_context_t& ctx)
{
    ImGui_ImplSDL3_ProcessEvent(&event);
    ImGuiIO& io = ImGui::GetIO();

    if (ctx.cur_mode == smol::editor_mode_e::EDIT) { return false; }

    bool ignore_mouse =
        io.WantCaptureMouse && (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                                event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_WHEEL);

    bool ignore_keyboard =
        io.WantCaptureKeyboard &&
        (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_TEXT_INPUT);

    return ignore_mouse || ignore_keyboard;
}

using smol::operator""_h;

bool open_project(const std::filesystem::path& project_file, smol::editor_context_t& ctx);

namespace
{
    enum class gate_state_e
    {
        PICKING,
        BUILDING,
    };

    gate_state_e g_gate = gate_state_e::PICKING;
    std::future<bool> g_build_future;
    std::filesystem::path g_pending_project;

    std::future<bool> g_recompile_future;

    bool project_needs_build(const std::filesystem::path& project_file, std::filesystem::path& out_dir)
    {
        smol::project_t project;
        if (!smol::project_t::load(project_file, project))
        {
            out_dir.clear();
            return false;
        }
        out_dir = project.project_dir;
        return !std::filesystem::exists(project.lib_path);
    }

    void request_open_project(const std::filesystem::path& project_file, smol::editor_context_t& ctx)
    {
        std::filesystem::path dir;
        if (project_needs_build(project_file, dir))
        {
            g_pending_project = project_file;
            g_gate = gate_state_e::BUILDING;
            ctx.project_loaded = false;
            smol::editor::project_manager::report_status("Building " + project_file.filename().string() + "...");
            const std::string eng = smol::editor::project_manager::engine_dir();
            g_build_future =
                std::async(std::launch::async, [pf = project_file, eng] { return build_project(pf, eng); });
        }
        else
        {
            ctx.project_loaded = open_project(project_file, ctx);
        }
    }
} // namespace

void update_editor_ui(smol::world_t& world, smol::editor_context_t& ctx)
{
    if (!ctx.project_loaded)
    {
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        if (g_gate == gate_state_e::BUILDING)
        {
            smol::editor::project_manager::draw_building(g_pending_project.filename().string());

            if (g_build_future.valid() && g_build_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                const bool built = g_build_future.get();
                g_gate = gate_state_e::PICKING;
                if (!built) { smol::editor::project_manager::report_status("Build failed, see console output"); }
                else if (open_project(g_pending_project, ctx)) { ctx.project_loaded = true; }
                else
                {
                    smol::editor::project_manager::report_status(
                        "Built, but the game library failed to load, see console output");
                }
            }
        }
        else
        {
            std::string chosen_project;
            if (smol::editor::project_manager::draw(ctx, chosen_project, nullptr))
            {
                request_open_project(chosen_project, ctx);
            }
        }

        ImGui::Render();
        smol::editor::imgui::submit(ImGui::GetDrawData());

        VkExtent2D extent = smol::renderer::ctx.render_extent;
        smol::renderer::submit_output_target("EditorViewport"_h, extent);

        auto& ecam = ctx.editor_camera;
        f32 aspect = extent.height ? (f32)extent.width / (f32)extent.height : 1.0f;
        smol::mat4_t view, proj, view_proj;
        smol::camera_system::build_view_projection(ecam.position, ecam.rotation.forward(), ecam.rotation.up(),
                                                   ecam.fov_deg, aspect, ecam.near_plane, ecam.far_plane, view, proj,
                                                   view_proj);
        smol::renderer::submit_color_view("PrimaryView"_h, view, proj, view_proj, ecam.position, "SceneColor"_h,
                                          "SceneDepth"_h, extent);
        return;
    }

    if (ctx.pending_scene_load)
    {
        ctx.pending_scene_load = false;
        std::ifstream file(ctx.pending_scene_path);
        if (file.is_open())
        {
            nlohmann::json scene_json = nlohmann::json::parse(file, nullptr, false);
            if (scene_json.is_discarded())
            {
                SMOL_LOG_ERROR("EDITOR", "Failed to parse scene file '{}'", ctx.pending_scene_path);
            }
            else
            {
                smol::serialization::clear_scene(world);
                smol::serialization::deserialize_scene(world, scene_json);
                ctx.current_scene_path = ctx.pending_scene_path;
                SMOL_LOG_INFO("EDITOR", "Scene loaded from {}", ctx.pending_scene_path);
            }
        }
        else
        {
            SMOL_LOG_ERROR("EDITOR", "Failed to open scene file '{}'", ctx.pending_scene_path);
        }
        ctx.pending_scene_path.clear();
    }

    if (ctx.pending_scene_save)
    {
        ctx.pending_scene_save = false;
        nlohmann::json scene_json = smol::serialization::serialize_scene(world);
        std::ofstream file(ctx.pending_scene_path);
        file << scene_json.dump(4);
        ctx.current_scene_path = ctx.pending_scene_path;
        SMOL_LOG_INFO("EDITOR", "Scene saved to {}", ctx.pending_scene_path);
        ctx.pending_scene_path.clear();
    }

    static std::chrono::steady_clock::time_point last_check_time = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_check_time).count() > 250)
    {
        last_check_time = now;

        std::error_code ec;
        auto cur_time = std::filesystem::last_write_time(trigger_path, ec);

        if (!ec && cur_time > last_reload_time)
        {
            SMOL_LOG_INFO("EDITOR", "Build system signaled completion. Attempting hot reload...");
            load_game_dll(true, ctx);
        }
    }

    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");

    static bool first_time = true;
    if (first_time)
    {
        first_time = false;

        if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr)
        {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

            f32 view_h = ImGui::GetMainViewport()->Size.y;

            ImGuiID dock_main = dockspace_id;

            ImGuiID dock_toolbar =
                ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Up, 32.0f / view_h, nullptr, &dock_main);
            ImGuiID dock_bottom =
                ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 32.0f / view_h, nullptr, &dock_main);
            ImGuiID dock_sidebars = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.15f, nullptr, &dock_main);

            ImGuiID dock_hierarchy =
                ImGui::DockBuilderSplitNode(dock_sidebars, ImGuiDir_Up, 0.5f, nullptr, &dock_sidebars);
            ImGuiID dock_inspector = dock_sidebars;

            ImGuiID dock_viewport = dock_main;

            ImGui::DockBuilderDockWindow("Toolbar", dock_toolbar);
            ImGui::DockBuilderDockWindow("Scene Viewport", dock_viewport);
            ImGui::DockBuilderDockWindow("Hierarchy", dock_hierarchy);
            ImGui::DockBuilderDockWindow("Inspector", dock_inspector);
            ImGui::DockBuilderDockWindow("Console", dock_bottom);

            ImGui::DockBuilderFinish(dockspace_id);
        }
    }

    ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport(), ImGuiDockNodeFlags_None);

    // ImGui::ShowDemoWindow();

    smol::editor::panels::draw_main_menu_bar(world, ctx);
    smol::editor::panels::draw_viewport(world, ctx);
    smol::editor::panels::draw_hierarchy(world, ctx);
    smol::editor::panels::draw_inspector(world, ctx);
    smol::editor::panels::draw_toolbar(world, ctx);
    smol::editor::panels::draw_console(world, ctx);

    for (smol::panel_draw_func custom_panel : ctx.custom_panels) { custom_panel(world, ctx); }

    if (ctx.recompile_requested && !ctx.recompiling)
    {
        ctx.recompile_requested = false;
        ctx.recompiling = true;
        const std::filesystem::path pf = ctx.project_file;
        const std::string eng = smol::editor::project_manager::engine_dir();
        g_recompile_future = std::async(std::launch::async, [pf, eng] { return build_project(pf, eng); });
    }
    if (ctx.recompiling && g_recompile_future.valid() &&
        g_recompile_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        const bool ok = g_recompile_future.get();
        ctx.recompiling = false;
        if (!ok) { SMOL_LOG_ERROR("EDITOR", "Recompile failed, see console output"); }
    }

    std::string chosen_project;
    bool commit_project = false;
    if (ctx.show_project_manager)
    {
        commit_project = smol::editor::project_manager::draw(ctx, chosen_project, &ctx.show_project_manager);
    }

    ImGui::Render();
    smol::editor::imgui::submit(ImGui::GetDrawData());

    smol::renderer::submit_output_target("EditorViewport"_h, {ctx.viewport_width, ctx.viewport_height});

    smol::engine::get_active_world().is_simulating = (ctx.cur_mode == smol::editor_mode_e::PLAY);

    static smol::editor_mode_e prev_mode = smol::editor_mode_e::EDIT;
    if (ctx.cur_mode != prev_mode)
    {
        smol::world_t& w = smol::engine::get_active_world();

        if (prev_mode == smol::editor_mode_e::EDIT && ctx.cur_mode == smol::editor_mode_e::PLAY)
        {
            ctx.world_backup = smol::serialization::serialize_scene(w);
        }
        else if (ctx.cur_mode == smol::editor_mode_e::EDIT)
        {
            smol::tween::cancel_all();

            smol::serialization::clear_scene(w);
            smol::serialization::deserialize_scene(w, ctx.world_backup);
            ctx.selected_entity = smol::ecs::NULL_ENTITY;
        }

        prev_mode = ctx.cur_mode;
    }

    if (ctx.cur_mode == smol::editor_mode_e::EDIT)
    {
        smol::editor::camera_system::update(ctx.editor_camera, ctx.is_viewport_hovered);

        auto& ecam = ctx.editor_camera;
        f32 aspect = (f32)smol::renderer::ctx.logical_extent.width / (f32)smol::renderer::ctx.logical_extent.height;
        smol::mat4_t view, proj, view_proj;
        smol::camera_system::build_view_projection(ecam.position, ecam.rotation.forward(), ecam.rotation.up(),
                                                   ecam.fov_deg, aspect, ecam.near_plane, ecam.far_plane, view, proj,
                                                   view_proj);

        smol::renderer::submit_color_view("PrimaryView"_h, view, proj, view_proj, ecam.position, "SceneColor"_h,
                                          "SceneDepth"_h, smol::renderer::ctx.render_extent);
    }
    else
    {
        if (ctx.cur_mode == smol::editor_mode_e::PLAY && ctx.game_update)
        {
            ctx.game_update(&smol::engine::get_active_world());
        }
    }

    if (commit_project)
    {
        ctx.show_project_manager = false;
        request_open_project(chosen_project, ctx);
    }
}

bool open_project(const std::filesystem::path& project_file, smol::editor_context_t& ctx)
{
    if (!std::filesystem::exists(project_file))
    {
        SMOL_LOG_ERROR("EDITOR", "Project file not found: {}", project_file.string());
        return false;
    }

    smol::project_t project;
    if (!smol::project_t::load(project_file, project))
    {
        SMOL_LOG_ERROR("EDITOR", "Failed to load project file: {}", project_file.string());
        return false;
    }

    source_lib_path = project.lib_path;
    trigger_path = project.trigger_path;
    source_lib_name = project.lib_path.string();
    ctx.project_file = project_file.string();

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(source_lib_path.parent_path(), ec))
    {
        if (entry.path().string().find("-loaded-") != std::string::npos) { std::filesystem::remove(entry.path(), ec); }
    }

    const std::filesystem::path proj_root = project_file.parent_path();
    const std::filesystem::path cooked = proj_root / ".smol";

    smol::vfs::mount("engine://assets/", (cooked / "engine").generic_string() + "/");
    smol::vfs::mount("game://assets/", (cooked / "game").generic_string() + "/");
    smol::vfs::mount("src://", (proj_root / "assets").generic_string() + "/");

    smol::asset_meta::load_guid_map((cooked / "guid_map.json").generic_string());

    smol::engine::create_scene();

    smol::engine::get_active_world().is_simulating = (ctx.cur_mode == smol::editor_mode_e::PLAY);

    ctx.project_dir = project.project_dir.string();

    if (!load_game_dll(false, ctx))
    {
        smol::engine::get_active_world().init();
        SMOL_LOG_ERROR("EDITOR", "Could not load the game lib (check the build output for errors)");
        return false;
    }

    smol::engine::get_active_world().init();

    if (!project.startup_scene.empty())
    {
        std::string vfs = project.startup_scene;
        if (vfs.find("://") == std::string::npos)
        {
            std::string_view guid_path = smol::asset_meta::get_path_for_guid(project.startup_scene);
            vfs = !guid_path.empty() ? std::string(guid_path) : ("game://assets/" + project.startup_scene);
        }

        std::string rel = vfs;
        const std::string prefix = "game://assets/";
        if (rel.rfind(prefix, 0) == 0) { rel = rel.substr(prefix.size()); }

        if (rel.size() > 10 && rel.compare(rel.size() - 10, 10, ".smolscene") == 0)
        {
            rel.replace(rel.size() - 10, 10, ".scene");
        }
        std::filesystem::path scene_src = project.assets_dir / rel;

        std::ifstream file(scene_src);
        if (file.is_open())
        {
            nlohmann::json scene_json = nlohmann::json::parse(file, nullptr, false);
            if (!scene_json.is_discarded())
            {
                smol::serialization::deserialize_scene(smol::engine::get_active_world(), scene_json);
                ctx.current_scene_path = scene_src.string();
                SMOL_LOG_INFO("EDITOR", "Opened startup scene: {}", scene_src.string());
            }
            else
            {
                SMOL_LOG_ERROR("EDITOR", "Startup scene is not valid: {}", scene_src.string());
            }
        }
        else
        {
            SMOL_LOG_WARN("EDITOR", "Startup scene not found: {}", scene_src.string());
        }
    }

    smol::editor::project_manager::add_recent(project_file.string());
    SMOL_LOG_INFO("EDITOR", "Opened project: {}", project_file.string());
    return true;
}

int main(i32 argc, char** argv)
{
    smol::log::init();

    std::filesystem::path startup_project;
    bool have_startup_project = false;
    if (argc >= 2)
    {
        startup_project = argv[1];
        have_startup_project = true;
    }

    if (!smol::engine::init("smol-editor", 1280, 720)) { return -1; }

    volkInitialize();
    volkLoadInstance(smol::renderer::ctx.instance);
    volkLoadDevice(smol::renderer::ctx.device);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.Fonts->AddFontDefaultVector();
    ImGui_ImplSDL3_InitForVulkan(smol::window::get_window());
    smol::editor::imgui::init();
    smol::editor::imgui::init_multiviewport();

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    smol::engine::create_scene();
    smol::engine::get_active_world().init();

    smol::engine::get_active_world().is_simulating = false;

    static smol::editor_context_t editor_ctx;

    if (have_startup_project) { request_open_project(startup_project, editor_ctx); }

    smol::engine::set_event_callback([&](const SDL_Event& event) { return process_editor_event(event, editor_ctx); });

    smol::engine::set_ui_callback([&]() { update_editor_ui(smol::engine::get_active_world(), editor_ctx); });

    smol::engine::set_post_render_callback(
        []()
        {
            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault(nullptr, nullptr);
            }
        });

    smol::engine::run();

    if (editor_ctx.game_shutdown) { editor_ctx.game_shutdown(&smol::engine::get_active_world()); }

    vkDeviceWaitIdle(smol::renderer::ctx.device);

    smol::editor::imgui::shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    smol::engine::shutdown();

    if (game_lib) { smol::os::free_lib(game_lib); }

    return 0;
}