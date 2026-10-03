#pragma once

#include "tau/assets/scene.h"
#include "tau/defines.h"
#include "tau/world.h"

#include "json/json.hpp"
#include <string>
#include <unordered_set>

namespace tau::serialization
{
    TAU_ENGINE_API nlohmann::json serialize_scene(tau::world_t& world);
    TAU_ENGINE_API void deserialize_scene(tau::world_t& world, const nlohmann::json& scene_data);

    TAU_ENGINE_API scene_t scene_from_json(const nlohmann::json& scene_data);
    TAU_ENGINE_API void write_scene_binary(const scene_t& scene, const std::string& output_path);
    TAU_ENGINE_API void instantiate_scene(tau::world_t& world, const scene_t& scene);

    TAU_ENGINE_API void clear_scene(tau::world_t& world);

    TAU_ENGINE_API std::unordered_set<u32_t> reflected_component_pool_ids(tau::world_t& world);

    // creates the pool of every registered component through code instantiated where each was registered
    // called with only engine types registered, it keeps engine pools out of the game DLL, see ensure_storage
    TAU_ENGINE_API void create_registered_pools(tau::world_t& world);

    struct reload_snapshot_t
    {
        nlohmann::json entities = nlohmann::json::array();
    };

    TAU_ENGINE_API reload_snapshot_t evict_game_components(tau::world_t& world,
                                                           const std::unordered_set<u32_t>& engine_pool_ids);

    TAU_ENGINE_API void restore_game_components(tau::world_t& world, const reload_snapshot_t& snapshot);
} // namespace tau::serialization