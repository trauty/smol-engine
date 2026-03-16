#pragma once

namespace smol { struct world_t; }

typedef void (*game_init_func)(smol::world_t*);
typedef void (*game_update_func)(smol::world_t*);
typedef void (*game_shutdown_func)(smol::world_t*);