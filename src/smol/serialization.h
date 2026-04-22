#pragma once

#include "smol/world.h"

#include "json/json.hpp"

namespace smol::serialization
{
    nlohmann::json serialize_scene(smol::world_t& world);
    void deserialize_scene(smol::world_t& world, const nlohmann::json& scene_data);

    void clear_scene(smol::world_t& world);
} // namespace smol::serialization