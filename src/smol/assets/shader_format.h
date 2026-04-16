#pragma once

#include "smol/defines.h"
namespace smol
{
    // SMSH
    constexpr u32_t SMOL_SHADER_MAGIC = 0x48534d53;

    enum class descriptor_type_e : u32_t
    {
        SAMPLER,
        SAMPLED_IMAGE,
        STORAGE_IMAGE,
        UNIFORM_BUFFER,
        STORAGE_BUFFER
    };

    struct shader_descriptor_binding_t
    {
        u32_t name_hash;
        u32_t set;
        u32_t binding;
        descriptor_type_e type;
        u32_t count;
    };

    struct shader_header_t
    {
        u32_t magic;
        bool is_compute;
        bool has_material_data;
        u32_t vert_spirv_size;
        u32_t frag_spirv_size;
        u32_t comp_spirv_size;
        u32_t target_format_count;
        u32_t descriptor_binding_count;
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