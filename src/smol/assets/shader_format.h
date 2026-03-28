#pragma once

#include "smol/defines.h"
namespace smol
{
    // SMSH
    constexpr u32_t SMOL_SHADER_MAGIC = 0x48534D53;

    struct shader_header_t
    {
        u32_t magic;
        bool is_compute;
        u32_t module_count;
        u32_t vert_spirv_size;
        u32_t frag_spirv_size;
        u32_t comp_spirv_size;
        u32_t target_format_count;
    };

    struct shader_module_header_t
    {
        char name[64];
        u32_t size;
        char target_pass[64];
        char blend_mode[32];
        bool depth_write;
        bool depth_test;
        u32_t member_count;
    };

    struct shader_member_header_t
    {
        u32_t name_hash;
        u32_t offset;
        u32_t size;
    };

} // namespace smol