#pragma once

#include "smol/defines.h"
#include "smol/ecs.h"

namespace smol::camera_system
{
    SMOL_API void set_fov(ecs::registry_t& reg, ecs::entity_t entity, f32 fov_deg);
    SMOL_API ecs::entity_t get_active_camera(ecs::registry_t& reg);

    SMOL_API void update(ecs::registry_t& reg);
} // namespace smol::camera_system