#pragma once

#include "smol/assets/scene.h"
#include "smol/world.h"

#include "json/json.hpp"
#include <string>

namespace smol::serialization
{
    nlohmann::json serialize_scene(smol::world_t& world);
    void deserialize_scene(smol::world_t& world, const nlohmann::json& scene_data);

    scene_t scene_from_json(const nlohmann::json& scene_data);
    void write_scene_binary(const scene_t& scene, const std::string& output_path);
    void instantiate_scene(smol::world_t& world, const scene_t& scene);

    void clear_scene(smol::world_t& world);
} // namespace smol::serialization