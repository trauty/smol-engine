#pragma once

#include "smol/defines.h"
#include "smol/ecs.h"

namespace smol::camera_system
{
    void set_fov(ecs::registry_t& reg, ecs::entity_t entity, f32 fov_deg);
    ecs::entity_t get_active_camera(ecs::registry_t& reg);

    void update(ecs::registry_t& reg);
} // namespace smol::camera_system