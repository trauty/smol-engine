#include "smol/asset_meta.h"
#include "smol/asset_registry.h"
#include "smol/assets/scene.h"
#include "smol/ecs_fwd.h"
#include "smol/engine.h"
#include "smol/game.h"
#include "smol/log.h"
#include "smol/project.h"
#include "smol/serialization.h"
#include "smol/vfs.h"
#include "smol/world.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_main.h>
#include <filesystem>

#ifdef SMOL_STATIC_LINK
extern "C" void smol_game_init(smol::world_t* world);
extern "C" void smol_game_update(smol::world_t* world);
extern "C" void smol_game_shutdown(smol::world_t* world);
#else
    #include "smol/os.h"
    #if SMOL_PLATFORM_WIN
        #include <windows.h>
    #elif SMOL_PLATFORM_LINUX || SMOL_PLATFORM_ANDROID
        #include <dlfcn.h>
    #endif

game_init_func game_init = nullptr;
game_update_func game_update = nullptr;
game_shutdown_func game_shutdown = nullptr;
smol::os::lib_handle_t game_lib = nullptr;
#endif

int main(int argc, char* argv[])
{
    smol::project_t project;
    bool have_project = argc >= 2 && smol::project_t::load(argv[1], project);

#ifndef SMOL_STATIC_LINK
    if (argc >= 2 && !have_project)
    {
        SMOL_LOG_FATAL("ENGINE", "Failed to load project file: {}", argv[1]);
        return -1;
    }
#endif

    const char* window_name = have_project ? project.project_name.c_str() : "smol";
    if (!smol::engine::init(window_name, 1280, 720)) { return -1; }

#if SMOL_PLATFORM_ANDROID
    smol::asset_meta::init("guid_map.json");
#else
    namespace fs = std::filesystem;
    const char* base_path = SDL_GetBasePath();
    std::string bp = base_path ? base_path : ".";
    while (bp.size() > 1 && (bp.back() == '/' || bp.back() == '\\')) { bp.pop_back(); }
    const fs::path exe_dir = bp;

    if (fs::exists(exe_dir / "assets" / "engine"))
    {
        smol::asset_meta::init((exe_dir / "assets" / "guid_map.json").generic_string());
    }
    else
    {
        const fs::path engine_assets = exe_dir.parent_path() / "share" / "smol" / "engine-assets";
        smol::asset_meta::init((engine_assets / "guid_map.json").generic_string());

        if (have_project)
        {
            const fs::path game_cook = project.cooked_assets_dir / "game";
            smol::vfs::mount("game://assets/", game_cook.generic_string() + "/");
            smol::asset_meta::init((project.cooked_assets_dir / "guid_map.json").generic_string());
        }
    }
#endif

    smol::engine::create_scene();
    smol::world_t& cur_world = smol::engine::get_active_world();

#ifdef SMOL_STATIC_LINK
    smol_game_init(&cur_world);
#else
    if (have_project)
    {
        std::string lib_name = project.lib_path.string();

        game_lib = smol::os::load_lib(lib_name.c_str());

        if (!game_lib)
        {
    #if SMOL_PLATFORM_WIN
            SMOL_LOG_FATAL("ENGINE", "Failed to load library with name: {}; Error: {}", lib_name, GetLastError());
    #elif SMOL_PLATFORM_LINUX || SMOL_PLATFORM_ANDROID
            SMOL_LOG_FATAL("ENGINE", "Failed to load library with name: {}; Error: {}", lib_name, dlerror());
    #endif
            return -1;
        }

        game_init = (game_init_func)smol::os::get_proc_address(game_lib, "smol_game_init_internal");
        game_update = (game_update_func)smol::os::get_proc_address(game_lib, "smol_game_update_internal");
        game_shutdown = (game_shutdown_func)smol::os::get_proc_address(game_lib, "smol_game_shutdown_internal");

        if (!game_init || !game_update || !game_shutdown)
        {
            SMOL_LOG_FATAL("ENGINE", "Could not find one or more game logic functions inside game lib");
            return -1;
        }

        game_init(&cur_world);
    }
    else
    {
        SMOL_LOG_WARN("ENGINE",
                      "No project file given; running an empty world. Usage: smol-runtime <path-to-.smolproject>");
    }
#endif

#ifdef SMOL_STARTUP_SCENE
    const std::string baked_startup_scene = SMOL_STARTUP_SCENE;
#else
    const std::string baked_startup_scene;
#endif
    std::string startup_scene_ref =
        (have_project && !project.startup_scene.empty()) ? project.startup_scene : baked_startup_scene;

    if (!startup_scene_ref.empty())
    {
        std::string scene_path = startup_scene_ref;
        if (scene_path.find("://") == std::string::npos)
        {
            std::string_view guid_path = smol::asset_meta::get_path_for_guid(startup_scene_ref);
            scene_path = !guid_path.empty() ? std::string(guid_path) : ("game://assets/" + startup_scene_ref);
        }

        smol::asset_registry_t& registry = smol::engine::get_asset_registry();
        smol::asset_handle_t scene_handle = registry.load_sync<smol::scene_t>(scene_path);

        if (smol::scene_t* scene = registry.get<smol::scene_t>(scene_handle))
        {
            smol::serialization::instantiate_scene(cur_world, *scene);
            SMOL_LOG_INFO("ENGINE", "Loaded startup scene: {}", scene_path);
        }
        else
        {
            SMOL_LOG_ERROR("ENGINE", "Failed to load startup scene: {}", scene_path);
        }

        registry.release<smol::scene_t>(scene_handle);
    }

    smol::engine::get_active_world().register_update_system(
        [](smol::ecs::registry_t& reg)
        {
#ifdef SMOL_STATIC_LINK
            smol_game_update(&smol::engine::get_active_world());
#else
            if (game_update) { game_update(&smol::engine::get_active_world()); }
#endif
        });

    cur_world.init();

    smol::engine::run();

#ifdef SMOL_STATIC_LINK
    smol_game_shutdown(&smol::engine::get_active_world());
#else
    if (game_shutdown) { game_shutdown(&smol::engine::get_active_world()); }
#endif

    smol::engine::shutdown();

#ifndef SMOL_STATIC_LINK
    if (game_lib) { smol::os::free_lib(game_lib); }
#endif

    return 0;
}
