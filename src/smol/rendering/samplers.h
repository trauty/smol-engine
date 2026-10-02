#pragma once

#include "smol/defines.h"
#include "smol/rendering/shader_shared.h"

namespace smol
{
    // the slots themselves are shared with the shaders; see shader_shared.h
    enum class sampler_type_e : u32_t
    {
        LINEAR_REPEAT = SMOL_SAMPLER_LINEAR_REPEAT,
        LINEAR_CLAMP = SMOL_SAMPLER_LINEAR_CLAMP,
        NEAREST_REPEAT = SMOL_SAMPLER_NEAREST_REPEAT,
        NEAREST_CLAMP = SMOL_SAMPLER_NEAREST_CLAMP,
        SHADOW_CMP = SMOL_SAMPLER_SHADOW_CMP, // depth comparison sampler, drives hardware pcf
        COUNT = SMOL_SAMPLER_COUNT,
    };
}