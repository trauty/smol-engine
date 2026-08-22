#pragma once

#include "smol/assets/scene.h"
#include "smol/defines.h"
#include "smol/world.h"

#include "json/json.hpp"
#include <string>
#include <unordered_set>

namespace smol::serialization
{
    SMOL_ENGINE_API nlohmann::json serialize_scene(smol::world_t& world);
    SMOL_ENGINE_API void deserialize_scene(smol::world_t& world, const nlohmann::json& scene_data);

    SMOL_ENGINE_API scene_t scene_from_json(const nlohmann::json& scene_data);
    SMOL_ENGINE_API void write_scene_binary(const scene_t& scene, const std::string& output_path);
    SMOL_ENGINE_API void instantiate_scene(smol::world_t& world, const scene_t& scene);

    SMOL_ENGINE_API void clear_scene(smol::world_t& world);

    SMOL_ENGINE_API std::unordered_set<u32_t> reflected_component_pool_ids(smol::world_t& world);

    struct reload_snapshot_t
    {
        nlohmann::json entities = nlohmann::json::array();
    };

    SMOL_ENGINE_API reload_snapshot_t evict_game_components(smol::world_t& world,
                                                            const std::unordered_set<u32_t>& engine_pool_ids);

    SMOL_ENGINE_API void restore_game_components(smol::world_t& world, const reload_snapshot_t& snapshot);
} // namespace smol::serialization