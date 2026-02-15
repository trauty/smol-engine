#pragma once

#include "smol/asset_registry.h"
#include "smol/ecs.h"
#include "smol/physics/physics_world.h"
#include <vector>
namespace smol
{
    using system_func_t = void (*)(ecs::registry_t&);

    struct world_t
    {
        std::string name;
        ecs::registry_t registry;
        physics_world_t physics;
        asset_registry_t* assets_reg = nullptr;

        std::vector<system_func_t> init_systems;
        std::vector<system_func_t> update_systems;
        std::vector<system_func_t> fixed_update_systems;
        std::vector<system_func_t> shutdown_systems;

        void init(asset_registry_t& engine_assets_reg);
        void update();
        void fixed_update();
        void shutdown();

        void register_init_system(system_func_t system);
        void register_update_system(system_func_t system);
        void register_fixed_update_system(system_func_t system);
        void register_shutdown_system(system_func_t system);
    };
} // namespace smol