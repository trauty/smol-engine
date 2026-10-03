#pragma once

#include "tau/defines.h"

namespace tau
{
    constexpr u32_t TAU_MATERIAL_MAGIC = 0x54414d54; // "TMAT"
    constexpr u32_t TAU_MATERIAL_VERSION = 1;

    struct material_header_t
    {
        u32_t magic = TAU_MATERIAL_MAGIC;
        u32_t version = TAU_MATERIAL_VERSION;
        u32_t shader_path_length;
        u32_t shader_guid_length;
        u32_t texture_count;
        u32_t sampler_count;
        u32_t property_count;
    };

    struct cooked_texture_bind_t
    {
        u32_t name_hash;
        u32_t path_length;
        u32_t guid_length;
    };

    struct cooked_sampler_bind_t
    {
        u32_t name_hash;
        u32_t sampler_value;
    };

    struct cooked_property_t
    {
        u32_t name_hash;
        u32_t data_size;
    };
} // namespace tau