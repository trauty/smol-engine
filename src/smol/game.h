#pragma once

namespace smol { struct world_t; }

extern "C"
{
    void smol_game_init(smol::world_t* world);
    void smol_game_update(smol::world_t* world);
    void smol_game_shutdown(smol::world_t* world);
}

typedef void (*game_init_func)(smol::world_t*);
typedef void (*game_update_func)(smol::world_t*);
typedef void (*game_shutdown_func)(smol::world_t*);