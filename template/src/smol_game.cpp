#include "smol/game.h"
#include "smol/log.h"
#include "smol/reflection.h"
#include "smol/rendering/renderer.h"
#include "smol/rendering/vulkan.h"
#include "smol/tween.h"
#include "smol/world.h"

using namespace smol;

void smol_game_register_types(smol::world_t* world) { smol::reflection::run_registrations(*world->reflection_ctx); }

void smol_game_init(smol::world_t* world) { SMOL_LOG_INFO("GAME", "Hello from ${NAME}"); }

void smol_game_update(smol::world_t* world) {}

void smol_game_shutdown(smol::world_t* world) {}

SMOL_GAME_ENTRY()