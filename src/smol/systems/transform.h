#pragma once

#include "smol/ecs.h"
#include "smol/math.h"

#include <vector>

namespace smol::transform_system
{
    void set_local_position(ecs::registry_t& reg, ecs::entity_t entity, vec3_t new_pos);
    void set_local_rotation(ecs::registry_t& reg, ecs::entity_t entity, quat_t new_rot);
    void set_local_scale(ecs::registry_t& reg, ecs::entity_t entity, vec3_t new_scale);

    vec3_t get_world_position(ecs::registry_t& reg, ecs::entity_t entity);
    vec3_t get_world_scale(ecs::registry_t& reg, ecs::entity_t entity);
    quat_t get_world_rotation(ecs::registry_t& reg, ecs::entity_t entity);

    void set_world_position(ecs::registry_t& reg, ecs::entity_t entity, vec3_t world_pos);

    void set_parent(ecs::registry_t& reg, ecs::entity_t child, ecs::entity_t parent);

    void update(ecs::registry_t& reg);
} // namespace smol::transform_system