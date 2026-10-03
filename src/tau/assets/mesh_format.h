#pragma once

#include "tau/defines.h"
#include "tau/math.h"

namespace tau
{
    constexpr u32_t TAU_MESH_MAGIC = 0x534d4d53;
    constexpr u32_t TAU_MESH_VERSION = 1;

    struct mesh_header_t
    {
        u32_t magic = TAU_MESH_MAGIC;
        u32_t version = TAU_MESH_VERSION;
        u32_t vertex_count;
        u32_t index_count;
        vec3_t local_center;
        f32 local_radius;
    };
} // namespace tau