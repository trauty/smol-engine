#pragma once

#include "smol/ecs_fwd.h"
#include "smol/math.h"

namespace smol::transform_system
{
    extern bool is_hierarchy_dirty;

    SMOL_API void set_local_position(ecs::registry_t& reg, ecs::entity_t entity, vec3_t new_pos);
    SMOL_API void set_local_rotation(ecs::registry_t& reg, ecs::entity_t entity, quat_t new_rot);
    SMOL_API void set_local_scale(ecs::registry_t& reg, ecs::entity_t entity, vec3_t new_scale);

    SMOL_API vec3_t get_world_position(ecs::registry_t& reg, ecs::entity_t entity);
    SMOL_API vec3_t get_world_scale(ecs::registry_t& reg, ecs::entity_t entity);
    SMOL_API quat_t get_world_rotation(ecs::registry_t& reg, ecs::entity_t entity);

    SMOL_API void set_world_position(ecs::registry_t& reg, ecs::entity_t entity, vec3_t world_pos);

    SMOL_API void set_parent(ecs::registry_t& reg, ecs::entity_t child, ecs::entity_t parent);

    void update(ecs::registry_t& reg);
} // namespace smol::transform_system