#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui_backend.h"
#include "imgui_internal.h"
#include "smol-editor/editor_context.h"
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
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/vulkan.h"
#include "smol/serialization.h"
#include "smol/systems/camera.h"
#include "smol/vfs.h"
#include "smol/window.h"
#include "smol/world.h"

#include "json/json.hpp"
#include <SDL3/SDL_events.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#if SMOL_PLATFORM_WIN
    #include <windows.h>
#elif SMOL_PLATFORM_LINUX || SMOL_PLATFORM_ANDROID
    #include <dlfcn.h>
#endif

std::filesystem::path source_lib_path;
std::filesystem::path trigger_path;
std::string source_lib_name;
std::string cur_temp_lib_name = "";

smol::os::lib_handle_t game_lib = nullptr;
std::filesystem::file_time_type last_reload_time;

typedef void (*editor_init_func)(smol::world_t*, smol::editor_context_t*, ImGuiContext*);

bool load_game_dll(bool is_reload, smol::editor_context_t& ctx)
{
    if (game_lib)
    {
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
#elif SMOL_PLATFORM_LINUX || SMOL_PLATFORM_ANDROID
        SMOL_LOG_FATAL("ENGINE", "Failed to load library with name: {}; Error: {}", cur_temp_lib_name, dlerror());
#endif
        return false;
    }

    ctx.game_init = (game_init_func)smol::os::get_proc_address(game_lib, "smol_game_init_internal");
    ctx.game_update = (game_update_func)smol::os::get_proc_address(game_lib, "smol_game_update_internal");
    ctx.game_shutdown = (game_shutdown_func)smol::os::get_proc_address(game_lib, "smol_game_shutdown_internal");

    editor_init_func editor_init = (editor_init_func)smol::os::get_proc_address(game_lib, "smol_editor_init");

    if (!ctx.game_init || !ctx.game_update || !ctx.game_shutdown)
    {
        SMOL_LOG_ERROR("EDITOR", "Could not find one or more game logic functions necessary");
        return false;
    }

    std::error_code trigger_ec;
    last_reload_time = std::filesystem::last_write_time(trigger_path, trigger_ec);

    if (!is_reload) { ctx.game_init(&smol::engine::get_active_world()); }

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

void update_editor_ui(smol::world_t& world, smol::editor_context_t& ctx)
{
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

        const char* ini_file = ImGui::GetIO().IniFilename;
        if (!ini_file || !std::filesystem::exists(ini_file))
        {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

            f32 view_w = ImGui::GetMainViewport()->Size.x;
            f32 view_h = ImGui::GetMainViewport()->Size.y;

            ImGuiID dock_main = dockspace_id;

            ImGuiID dock_toolbar =
                ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Up, 32.0f / view_h, nullptr, &dock_main);
            ImGuiID dock_sidebars = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.15f, nullptr, &dock_main);

            ImGuiID dock_hierarchy =
                ImGui::DockBuilderSplitNode(dock_sidebars, ImGuiDir_Up, 0.5f, nullptr, &dock_sidebars);
            ImGuiID dock_inspector = dock_sidebars;

            ImGuiID dock_viewport = dock_main;

            ImGui::DockBuilderDockWindow("Toolbar", dock_toolbar);
            ImGui::DockBuilderDockWindow("Scene Viewport", dock_viewport);
            ImGui::DockBuilderDockWindow("Hierarchy", dock_hierarchy);
            ImGui::DockBuilderDockWindow("Inspector", dock_inspector);

            ImGuiDockNode* toolbar_node = ImGui::DockBuilderGetNode(dock_toolbar);
            if (toolbar_node) { toolbar_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar; }

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

    for (smol::panel_draw_func custom_panel : ctx.custom_panels) { custom_panel(world, ctx); }

    std::string chosen_project;
    bool commit_project = false;
    if (ctx.show_project_manager)
    {
        commit_project = smol::editor::project_manager::draw(ctx, chosen_project, &ctx.show_project_manager);
    }

    ImGui::Render();
    smol::editor::imgui::submit(ImGui::GetDrawData());

    smol::renderer::submit_output_target("EditorViewport"_h, {ctx.viewport_width, ctx.viewport_height});

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
        if (!open_project(chosen_project, ctx)) { ctx.show_project_manager = true; }
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

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(source_lib_path.parent_path(), ec))
    {
        if (entry.path().string().find("-loaded-") != std::string::npos) { std::filesystem::remove(entry.path(), ec); }
    }

    const std::filesystem::path proj_root = project_file.parent_path();
    const std::filesystem::path staged_engine = proj_root / ".smol" / "engine";

    smol::vfs::mount("game://assets/", (proj_root / ".smol" / "game").generic_string() + "/");
    smol::vfs::mount("src://", (proj_root / "assets").generic_string() + "/");

    if (std::filesystem::exists(staged_engine))
    {
        smol::vfs::mount("engine://assets/", staged_engine.generic_string() + "/");
        smol::asset_meta::init((proj_root / ".smol" / "guid_map.json").generic_string());
    }
    else
    {
        std::string em = smol::vfs::resolve("engine://assets/");
        while (em.size() > 1 && (em.back() == '/' || em.back() == '\\')) { em.pop_back(); }
        smol::asset_meta::init((std::filesystem::path(em).parent_path() / "guid_map.json").generic_string());
        smol::asset_meta::init((proj_root / ".smol" / "guid_map.json").generic_string());
    }

    smol::engine::create_scene();
    ctx.project_dir = project.project_dir.string();

    if (!load_game_dll(false, ctx))
    {
        smol::engine::get_active_world().init();
        SMOL_LOG_ERROR("EDITOR", "Could not load the game lib -- has the project been built? Staying on the manager.");
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
    io.Fonts->AddFontDefaultVector();
    ImGui_ImplSDL3_InitForVulkan(smol::window::get_window());
    smol::editor::imgui::init();

    smol::engine::create_scene();
    smol::engine::get_active_world().init();

    static smol::editor_context_t editor_ctx;

    bool startup_opened = false;
    if (have_startup_project) { startup_opened = open_project(startup_project, editor_ctx); }
    editor_ctx.show_project_manager = !startup_opened;

    smol::engine::set_event_callback([&](const SDL_Event& event) { return process_editor_event(event, editor_ctx); });

    smol::engine::set_ui_callback([&]() { update_editor_ui(smol::engine::get_active_world(), editor_ctx); });

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