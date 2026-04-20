#pragma once

namespace smol { struct world_t; }

typedef void (*game_init_func)(smol::world_t*);
typedef void (*game_update_func)(smol::world_t*);
typedef void (*game_shutdown_func)(smol::world_t*);

#ifndef SMOL_STATIC_LINK
    #define SMOL_GAME_ENTRY()                                                                                          \
        extern "C"                                                                                                     \
        {                                                                                                              \
            SMOL_API void smol_game_init_internal(smol::world_t* world)                                                \
            {                                                                                                          \
                volkInitialize();                                                                                      \
                volkLoadInstance(smol::renderer::ctx.instance);                                                        \
                volkLoadDevice(smol::renderer::ctx.device);                                                            \
                smol_game_init(world);                                                                                 \
            }                                                                                                          \
            SMOL_API void smol_game_update_internal(smol::world_t* world) { smol_game_update(world); }                 \
            SMOL_API void smol_game_shutdown_internal(smol::world_t* world) { smol_game_shutdown(world); }             \
        }
#else
    #define SMOL_GAME_ENTRY()
#endif