#pragma once

#include "smol/defines.h"

namespace smol { struct world_t; }

typedef void (*game_register_types_func)(smol::world_t*);
typedef void (*game_init_func)(smol::world_t*);
typedef void (*game_update_func)(smol::world_t*);
typedef void (*game_shutdown_func)(smol::world_t*);

extern "C"
{
    void smol_game_register_types(smol::world_t* world); // only for registering types, no game logic here
    void smol_game_init(smol::world_t* world);
    void smol_game_update(smol::world_t* world);
    void smol_game_shutdown(smol::world_t* world);
}

namespace smol::game
{
    // code run each time the game library loads (first time and every hot reload) to register what points at its code
    // such as input actions and render features, which the engine removes on unload
    // smol_game_init runs once, so use SMOL_ON_LOAD() { ... }
    using on_load_fn = void (*)(smol::world_t& world);

    SMOL_ENGINE_API void add_on_load(on_load_fn fn);
    SMOL_ENGINE_API void run_on_load(smol::world_t& world);
    SMOL_ENGINE_API u32_t remove_on_load_of(void* module_base);

    struct on_load_registrar_t
    {
        explicit on_load_registrar_t(on_load_fn fn) { add_on_load(fn); }
    };
} // namespace smol::game

#define SMOL_ON_LOAD_CAT2(a, b) a##b
#define SMOL_ON_LOAD_CAT(a, b) SMOL_ON_LOAD_CAT2(a, b)
#define SMOL_ON_LOAD_IMPL(id)                                                                                          \
    static void SMOL_ON_LOAD_CAT(smol_on_load_, id)(smol::world_t & world);                                            \
    static const smol::game::on_load_registrar_t SMOL_ON_LOAD_CAT(smol_on_load_reg_,                                   \
                                                                  id)(&SMOL_ON_LOAD_CAT(smol_on_load_, id));           \
    static void SMOL_ON_LOAD_CAT(smol_on_load_, id)([[maybe_unused]] smol::world_t & world)
#define SMOL_ON_LOAD() SMOL_ON_LOAD_IMPL(__COUNTER__)

#ifndef SMOL_STATIC_LINK
  // what the entry points below call, so a game compiles with this header alone
    #include "smol/rendering/renderer.h"

    #include <volk.h>

    #define SMOL_GAME_ENTRY()                                                                                          \
        extern "C"                                                                                                     \
        {                                                                                                              \
            SMOL_GAME_API void smol_game_register_types_internal(smol::world_t* world)                                 \
            { smol_game_register_types(world); }                                                                       \
            SMOL_GAME_API void smol_game_init_internal(smol::world_t* world)                                           \
            {                                                                                                          \
                volkInitialize();                                                                                      \
                volkLoadInstance(smol::renderer::ctx.instance);                                                        \
                volkLoadDevice(smol::renderer::ctx.device);                                                            \
                smol_game_init(world);                                                                                 \
            }                                                                                                          \
            SMOL_GAME_API void smol_game_update_internal(smol::world_t* world) { smol_game_update(world); }            \
            SMOL_GAME_API void smol_game_shutdown_internal(smol::world_t* world) { smol_game_shutdown(world); }        \
        }
#else
    #define SMOL_GAME_ENTRY()
#endif