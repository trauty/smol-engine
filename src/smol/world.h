#pragma once

#include "smol/ecs.h"
#include "smol/physics/physics_world.h"

#include <vector>

namespace entt { struct meta_ctx; }

namespace smol
{
    namespace reflection { using ctx_t = entt::meta_ctx; }

    using system_func_t = void (*)(ecs::registry_t&);

    struct SMOL_API world_t
    {
        std::string name;
        ecs::registry_t registry;
        reflection::ctx_t* reflection_ctx;
        physics_world_t physics;

        std::vector<system_func_t> init_systems;
        std::vector<system_func_t> update_systems;
        std::vector<system_func_t> fixed_update_systems;
        std::vector<system_func_t> shutdown_systems;

        void init();
        void update();
        void fixed_update();
        void shutdown();

        void register_init_system(system_func_t system);
        void register_update_system(system_func_t system);
        void register_fixed_update_system(system_func_t system);
        void register_shutdown_system(system_func_t system);
    };
} // namespace smol