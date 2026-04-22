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
#include "smol-editor/systems/camera.h"
#include "smol/components/camera.h"
#include "smol/components/transform.h"
#include "smol/ecs.h"
#include "smol/engine.h"
#include "smol/game.h"
#include "smol/hash.h"
#include "smol/log.h"
#include "smol/os.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/renderer_types.h"
#include "smol/rendering/vulkan.h"
#include "smol/vfs.h"
#include "smol/window.h"
#include "smol/world.h"

#include "json/json.hpp"
#include <SDL3/SDL_events.h>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#if SMOL_PLATFORM_WIN
    #include <windows.h>
const std::string LIB_PREFIX = "";
const std::string LIB_EXT = ".dll";
#elif SMOL_PLATFORM_LINUX || SMOL_PLATFORM_ANDROID
    #include <dlfcn.h>
const std::string LIB_PREFIX = "lib";
const std::string LIB_EXT = ".so";
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

void update_editor_ui(smol::world_t& world, smol::editor_context_t& ctx)
{
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
                ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Up, 48.0f / view_h, nullptr, &dock_main);
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

    ImGui::Render();
    smol::editor::imgui::submit(ImGui::GetDrawData());

    if (ctx.cur_mode == smol::editor_mode_e::EDIT)
    {
        smol::editor::camera_system::update(world.registry, ctx.is_viewport_hovered);
    }
    else if (ctx.cur_mode == smol::editor_mode_e::PLAY)
    {
        if (ctx.game_update) { ctx.game_update(&smol::engine::get_active_world()); }
    }
}

int main(i32 argc, char** argv)
{
    smol::log::init();

    if (argc < 2)
    {
        SMOL_LOG_INFO("EDITOR", "Usage: smol-editor <path-to-project-file>");
        return -1;
    }

    std::filesystem::path project_file_path = argv[1];
    if (!std::filesystem::exists(project_file_path))
    {
        SMOL_LOG_FATAL("EDITOR", "Project file not found: {}", project_file_path.string());
        return -1;
    }

    std::ifstream project_file(project_file_path);
    nlohmann::json project_data;
    try
    {
        project_data = nlohmann::json::parse(project_file);
    }
    catch (std::exception exc)
    {
        SMOL_LOG_FATAL("EDITOR", "Project file not valid: {}", exc.what());
        return -1;
    }

    std::string lib_base_name = project_data.value("game_lib_name", "smol-game-logic");
    std::string bin_dir = project_data["paths"].value("bin_dir", "bin");

    std::string full_lib_name = LIB_PREFIX + lib_base_name + LIB_EXT;

    std::filesystem::path project_dir = std::filesystem::absolute(project_file_path).parent_path();
    source_lib_path = project_dir / bin_dir / full_lib_name;

    trigger_path = source_lib_path.string() + ".trigger";
    source_lib_name = source_lib_path.string();

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(source_lib_path.parent_path(), ec))
    {
        if (entry.path().string().find("-loaded-") != std::string::npos) { std::filesystem::remove(entry.path(), ec); }
    }

    if (!smol::engine::init("smol-editor", 1280, 720)) { return -1; }

    smol::vfs::mount("engine://assets/", project_file_path.parent_path().generic_string() + "/.smol/engine/");
    smol::vfs::mount("game://assets/", project_file_path.parent_path().generic_string() + "/.smol/game/");

    volkInitialize();
    volkLoadInstance(smol::renderer::ctx.instance);
    volkLoadDevice(smol::renderer::ctx.device);

    smol::renderer::set_use_offscreen_viewport(true);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.Fonts->AddFontDefaultVector();
    ImGui_ImplSDL3_InitForVulkan(smol::window::get_window());
    smol::editor::imgui::init();

    smol::engine::create_scene();
    smol::world_t& cur_world = smol::engine::get_active_world();

    static smol::editor_context_t editor_ctx;

    if (!load_game_dll(false, editor_ctx))
    {
        SMOL_LOG_ERROR("EDITOR", "Initial load of game lib failed. Exiting...");
        return -1;
    }

    smol::engine::set_event_callback([&](const SDL_Event& event) { return process_editor_event(event, editor_ctx); });
    smol::engine::set_ui_callback([&]() { update_editor_ui(cur_world, editor_ctx); });

    cur_world.init();

    editor_ctx.editor_camera = cur_world.registry.create();
    cur_world.registry.emplace<smol::transform_t>(editor_ctx.editor_camera);
    cur_world.registry.emplace<smol::camera_t>(editor_ctx.editor_camera);
    cur_world.registry.emplace<smol::editor::editor_camera_tag>(editor_ctx.editor_camera);
    cur_world.registry.emplace<smol::active_camera_tag>(editor_ctx.editor_camera);

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