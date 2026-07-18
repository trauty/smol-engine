#include "smol/game.h"
#include "smol/log.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/vulkan.h"
#include "smol/world.h"

using namespace smol;

void smol_game_init(smol::world_t* world) { SMOL_LOG_INFO("GAME", "Hello from ${NAME}"); }

void smol_game_update(smol::world_t* world) {}

void smol_game_shutdown(smol::world_t* world) {}

SMOL_GAME_ENTRY()