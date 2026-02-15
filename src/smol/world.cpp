#include "world.h"
#include "smol/asset_registry.h"
#include "smol/physics/physics_sync.h"
#include "smol/physics/physics_world.h"

namespace smol
{
    void world_t::init(asset_registry_t& engine_assets_reg)
    {
        this->assets_reg = &engine_assets_reg;

        registry.set_context<asset_registry_t>(&engine_assets_reg);
        registry.set_context<physics_world_t>(&physics);

        physics.init(registry);

        for (system_func_t& system : init_systems) { system(registry); }
    }

    void world_t::update()
    {
        for (system_func_t& system : update_systems) { system(registry); }
    }

    void world_t::fixed_update()
    {
        physics.create_bodies(registry);

        for (system_func_t& system : fixed_update_systems) { system(registry); }

        physics::sync_to_physics(registry, physics);
        physics.update();
        physics::sync_from_physics(registry, physics);
    }

    void world_t::shutdown()
    {
        for (system_func_t& system : shutdown_systems) { system(registry); }

        physics.destroy_bodies(registry);
        physics.shutdown();
    }

    void world_t::register_init_system(system_func_t system) { init_systems.push_back(system); }
    void world_t::register_update_system(system_func_t system) { update_systems.push_back(system); }
    void world_t::register_fixed_update_system(system_func_t system) { fixed_update_systems.push_back(system); }
    void world_t::register_shutdown_system(system_func_t system) { shutdown_systems.push_back(system); }
} // namespace smol