#include "smol/ecs_fwd.h"
#include "smol/engine.h"
#include "smol/game.h"
#include "smol/world.h"

#ifdef SMOL_STATIC_LINK
extern "C" void smol_game_init(smol::world_t* world);
extern "C" void smol_game_update(smol::world_t* world);
extern "C" void smol_game_shutdown(smol::world_t* world);
#else
    #include "smol/log.h"
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

int main()
{
    if (!smol::engine::init(SMOL_GAME_NAME, 1280, 720)) { return -1; }

    smol::engine::create_scene();
    smol::world_t& cur_world = smol::engine::get_active_world();

#ifdef SMOL_STATIC_LINK
    smol_game_init(&cur_world);
#else
    std::string lib_name = SMOL_LIB_PATH;

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

    game_init = (game_init_func)smol::os::get_proc_address(game_lib, "smol_game_init");
    game_update = (game_update_func)smol::os::get_proc_address(game_lib, "smol_game_update");
    game_shutdown = (game_shutdown_func)smol::os::get_proc_address(game_lib, "smol_game_shutdown");

    if (!game_init || !game_update || !game_shutdown)
    {
        SMOL_LOG_FATAL("ENGINE", "Could not find one or more game logic functions inside game lib");
        return -1;
    }

    game_init(&cur_world);
#endif

    smol::engine::get_active_world().register_update_system(
        [](smol::ecs::registry_t& reg)
        {
#ifdef SMOL_STATIC_LINK
            smol_game_update(&smol::engine::get_active_world());
#else
            game_update(&smol::engine::get_active_world());
#endif
        });

    cur_world.init();

    smol::engine::run();

#ifdef SMOL_STATIC_LINK
    smol_game_shutdown(&smol::engine::get_active_world());
#else
    game_shutdown(&smol::engine::get_active_world());
    smol::os::free_lib(game_lib);
#endif

    smol::engine::shutdown();
}