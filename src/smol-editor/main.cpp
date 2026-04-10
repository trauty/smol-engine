
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
#include "imgui_backend.h"
#include "smol/engine.h"
#include "smol/game.h"
#include "smol/input.h"
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

game_init_func game_init = nullptr;
game_update_func game_update = nullptr;
game_shutdown_func game_shutdown = nullptr;

smol::os::lib_handle_t game_lib = nullptr;
std::filesystem::file_time_type last_reload_time;

bool load_game_dll(bool is_reload)
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

    game_init = (game_init_func)smol::os::get_proc_address(game_lib, "smol_game_init");
    game_update = (game_update_func)smol::os::get_proc_address(game_lib, "smol_game_update");
    game_shutdown = (game_shutdown_func)smol::os::get_proc_address(game_lib, "smol_game_shutdown");

    if (!game_init || !game_update || !game_shutdown)
    {
        SMOL_LOG_ERROR("EDITOR", "Could not find one or more game logic functions necessary");
        return false;
    }

    std::error_code trigger_ec;
    last_reload_time = std::filesystem::last_write_time(trigger_path, trigger_ec);

    if (!is_reload) { game_init(&smol::engine::get_active_world()); }

    SMOL_LOG_INFO("EDITOR", "{}", is_reload ? "Successfully hot-reloaded game lib" : "Successfully loaded game lib");
    return true;
}

struct editor_t
{
    std::chrono::steady_clock::time_point last_check_time = std::chrono::steady_clock::now();

    bool process_event(const SDL_Event& event)
    {
        ImGui_ImplSDL3_ProcessEvent(&event);
        ImGuiIO& io = ImGui::GetIO();

        bool ignore_mouse = io.WantCaptureMouse &&
                            (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                             event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_WHEEL);

        bool ignore_keyboard =
            io.WantCaptureKeyboard &&
            (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_TEXT_INPUT);

        return ignore_mouse || ignore_keyboard;
    }

    void update(smol::world_t& world)
    {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_check_time).count() > 250)
        {
            last_check_time = now;

            std::error_code ec;
            auto cur_time = std::filesystem::last_write_time(trigger_path, ec);

            if (!ec && cur_time > last_reload_time)
            {
                SMOL_LOG_INFO("EDITOR", "Build system signaled completion. Attempting hot reload...");
                load_game_dll(true);
            }
        }

        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockspace_flags);

        ImGui::ShowDemoWindow();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
        if (ImGui::Begin("Scene Viewport"))
        {
            ImVec2 viewport_size = ImGui::GetContentRegionAvail();
            ImVec2 viewport_pos = ImGui::GetCursorScreenPos();

            static ImVec2 last_size = {0.0f, 0.0f};
            if (viewport_size.x > 0.0f && viewport_size.y > 0.0f &&
                (viewport_size.x != last_size.x || viewport_size.y != last_size.y))
            {
                smol::renderer::set_render_resolution((u32_t)viewport_size.x, (u32_t)viewport_size.y);
                last_size = viewport_size;
            }

            smol::input::set_viewport_offset(viewport_pos.x, viewport_pos.y);
            smol::input::set_viewport_size(viewport_size.x, viewport_size.y);

            u32_t tex_id = smol::renderer::get_viewport_texture_id();
            if (tex_id != smol::renderer::BINDLESS_NULL_HANDLE)
            {
                ImGui::Image((ImTextureID)(intptr_t)(tex_id + 1), viewport_size);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::Render();
        smol::editor::imgui::submit(ImGui::GetDrawData());

        if (game_update) { game_update(&smol::engine::get_active_world()); }
    }
};

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
    ImGui_ImplSDL3_InitForVulkan(smol::window::get_window());
    smol::editor::imgui::init();

    smol::engine::create_scene();
    smol::world_t& cur_world = smol::engine::get_active_world();

    if (!load_game_dll(false))
    {
        SMOL_LOG_ERROR("EDITOR", "Initial load of game lib failed. Exiting...");
        return -1;
    }

    static editor_t editor;

    smol::engine::set_event_callback([](const SDL_Event& event) { return editor.process_event(event); });
    smol::engine::set_ui_callback([]() { editor.update(smol::engine::get_active_world()); });

    cur_world.init();
    smol::engine::run();

    if (game_shutdown) { game_shutdown(&smol::engine::get_active_world()); }

    vkDeviceWaitIdle(smol::renderer::ctx.device);

    smol::editor::imgui::shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    smol::engine::shutdown();

    if (game_lib) { smol::os::free_lib(game_lib); }

    return 0;
}