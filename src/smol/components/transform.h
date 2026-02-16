#pragma once

#include "smol/defines.h"
#include "smol/ecs_types.h"
#include "smol/math.h"

namespace smol
{
    struct transform_t
    {
        SMOL_COMPONENT(transform_t);

        vec3_t local_position;
        quat_t local_rotation;
        vec3_t local_scale = {1.0f, 1.0f, 1.0f};

        mat4_t local_mat;
        mat4_t world_mat;

        ecs::entity_t parent = ecs::NULL_ENTITY;
        ecs::entity_t first_child = ecs::NULL_ENTITY;
        ecs::entity_t next_sibling = ecs::NULL_ENTITY;
        ecs::entity_t prev_sibling = ecs::NULL_ENTITY;

        i32_t parent_dense_index = -1;
        bool is_dirty = false;
    };
} // namespace smol