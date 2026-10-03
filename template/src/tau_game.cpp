#include "tau/game.h"
#include "tau/log.h"
#include "tau/reflection.h"
#include "tau/rendering/renderer.h"
#include "tau/rendering/vulkan.h"
#include "tau/tween.h"
#include "tau/world.h"

using namespace tau;

void tau_game_register_types(tau::world_t* world) { tau::reflection::run_registrations(*world->reflection_ctx); }

void tau_game_init(tau::world_t* world) { TAU_LOG_INFO("GAME", "Hello from ${NAME}"); }

void tau_game_update(tau::world_t* world) {}

void tau_game_shutdown(tau::world_t* world) {}

TAU_GAME_ENTRY()