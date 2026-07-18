#pragma once

#include "smol/defines.h"

namespace smol
{
    constexpr u32_t SMOL_SCENE_MAGIC = 0x4e435353; // "SSCN"
    constexpr u32_t SMOL_SCENE_VERSION = 1;

    enum class scene_value_type_e : u8_t
    {
        I32 = 0,
        U32 = 1,
        F32 = 2,
        BOOL = 3,
        STRING = 4,
        VEC3 = 5,
        ASSET_REF = 6,
    };

    struct scene_header_t
    {
        u32_t magic = SMOL_SCENE_MAGIC;
        u32_t version = SMOL_SCENE_VERSION;
        u32_t entity_count = 0;
    };
} // namespace smol
