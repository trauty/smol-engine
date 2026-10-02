// The smallest game the engine runs. One component of its own, so opening the sample exercises
// game code, reflection, the inspector and hot reload, not only the renderer: edit bob_t's
// update, press Recompile, and the sphere moves differently without restarting.
#include "smol/components/transform.h"
#include "smol/ecs.h"
#include "smol/game.h"
#include "smol/input.h"
#include "smol/reflection.h"
#include "smol/time.h"
#include "smol/world.h"

#include <cmath>

using namespace smol;

// moves an entity up and down around the height it was placed at
struct bob_t
{
    f32 height = 0.5f;
    f32 speed = 1.5f;
    f32 base_y = 1.0f;
};

SMOL_REFLECT()
{
    reflection::component<bob_t>(ctx, "Bob")
        .field<&bob_t::height>("Height")
        .field<&bob_t::speed>("Speed")
        .field<&bob_t::base_y>("Base Y");
}

namespace { bool g_paused = false; }

// Space pauses the bobbing in Play. Registered per load, not in smol_game_init: the engine drops
// a library's input listeners when a hot reload unloads it, and this registers the new code's.
SMOL_ON_LOAD()
{
    input::bind_button("pause_bob", input::key_e::Space);
    input::on_action("pause_bob", input::input_state_t::PRESSED,
                     [](const input::input_context_t&) { g_paused = !g_paused; });
}

void smol_game_register_types(world_t* world) { reflection::run_registrations(*world->reflection_ctx); }

void smol_game_init(world_t* world) {}

void smol_game_update(world_t* world)
{
    if (g_paused) { return; }

    const f32 t = static_cast<f32>(time::get_time());

    for (auto [entity, bob, transform] : world->registry.view<bob_t, transform_t>().each())
    {
        transform.local_position.y = bob.base_y + bob.height * std::sin(t * bob.speed);
        transform.is_dirty = true;
    }
}

void smol_game_shutdown(world_t* world) {}

SMOL_GAME_ENTRY()
