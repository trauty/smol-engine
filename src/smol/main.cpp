#include "smol/defines.h"
#include "smol/ecs_fwd.h"
#include "smol/engine.h"
#include "smol/game.h"
#include "smol/log.h"
#include "smol/os.h"
#include "smol/world.h"

#include <memory>

#if SMOL_PLATFORM_WIN
    #include <windows.h>
#elif SMOL_PLATFORM_LINUX || SMOL_PLATFORM_ANDROID
    #include <dlfcn.h>
#endif

game_update_func game_update = nullptr;

int main()
{
    if (!smol::engine::init("smol-engine", 1280, 720)) { return -1; }

#if SMOL_PLATFORM_WIN
    const char* lib_name = "smol-game.dll";
#elif SMOL_PLATFORM_LINUX || SMOL_PLATFORM_ANDROID
    const char* lib_name = "./libsmol-game.so";
#endif

    smol::os::lib_handle_t game_lib = smol::os::load_lib(lib_name);

    if (!game_lib)
    {
#if SMOL_PLATFORM_WINDOWS
        SMOL_LOG_FATAL("ENGINE", "Failed to load library with name: {}; Error: {}", lib_name, GetLastError());
#elif SMOL_PLATFORM_LINUX || SMOL_PLATFORM_ANDROID
        SMOL_LOG_FATAL("ENGINE", "Failed to load library with name: {}; Error: {}", lib_name, dlerror());
#endif
        return -1;
    }

    game_init_func game_init = (game_init_func)smol::os::get_proc_address(game_lib, "smol_game_init");
    game_update = (game_update_func)smol::os::get_proc_address(game_lib, "smol_game_update");
    game_shutdown_func game_shutdown = (game_shutdown_func)smol::os::get_proc_address(game_lib, "smol_game_shutdown");

    std::unique_ptr<smol::world_t> world = std::make_unique<smol::world_t>();
    smol::engine::set_scene(std::move(world));

    game_init(&smol::engine::get_active_world());

    smol::engine::get_active_world().register_update_system([](smol::ecs::registry_t& reg)
                                                            { game_update(&smol::engine::get_active_world()); });

    smol::engine::run();

    game_shutdown(&smol::engine::get_active_world());
    smol::engine::shutdown();

    smol::os::free_lib(game_lib);
}