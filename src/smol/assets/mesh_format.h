#pragma once

#include "smol/defines.h"
#include "smol/math.h"

namespace smol
{
    constexpr u32_t SMOL_MESH_MAGIC = 0x534d4d53;

    struct mesh_header_t
    {
        u32_t magic;
        u32_t vertex_count;
        u32_t index_count;
        vec3_t local_center;
        f32 local_radius;
        u32_t _pad;
    };
} // namespace smol