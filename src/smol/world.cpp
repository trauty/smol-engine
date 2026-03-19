#include "world.h"

#include "smol/components/transform.h"
#include "smol/ecs_fwd.h"
#include "smol/physics/physics_sync.h"
#include "smol/physics/physics_world.h"
#include "smol/systems/transform.h"
#include "tracy/Tracy.hpp"

namespace smol
{
    void on_transform_changed(ecs::registry_t& reg, ecs::entity_t entity)
    { smol::transform_system::is_hierarchy_dirty = true; }

    void world_t::init()
    {
        registry.ctx().emplace<physics_world_t*>(&physics);

        registry.on_construct<transform_t>().connect<&on_transform_changed>();
        registry.on_destroy<transform_t>().connect<&on_transform_changed>();

        physics.init(registry);

        for (system_func_t& system : init_systems) { system(registry); }
    }

    void world_t::update()
    {
        ZoneScoped;
        for (system_func_t& system : update_systems) { system(registry); }
    }

    void world_t::fixed_update()
    {
        ZoneScoped;
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

        registry.clear();
        registry.ctx().clear();
    }

    void world_t::register_init_system(system_func_t system) { init_systems.push_back(system); }
    void world_t::register_update_system(system_func_t system) { update_systems.push_back(system); }
    void world_t::register_fixed_update_system(system_func_t system) { fixed_update_systems.push_back(system); }
    void world_t::register_shutdown_system(system_func_t system) { shutdown_systems.push_back(system); }
} // namespace smol