#pragma once

#include "smol/defines.h"

namespace smol
{
    constexpr u32_t SMOL_MATERIAL_MAGIC = 0x54414d53;

    struct material_header_t
    {
        u32_t magic = SMOL_MATERIAL_MAGIC;
        u32_t version = 1; // need to add this to the other formats
        u32_t shader_path_length;
        u32_t texture_count;
        u32_t property_count;
    };

    struct cooked_texture_bind_t
    {
        u32_t name_hash;
        u32_t path_length;
    };

    struct cooked_property_t
    {
        u32_t name_hash;
        u32_t data_size;
    };
} // namespace smol