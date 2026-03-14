#pragma once

#include "smol/defines.h"
namespace smol
{
    enum class sampler_type_e : u32_t
    {
        LINEAR_REPEAT = 0,
        LINEAR_CLAMP,
        NEAREST_REPEAT,
        NEAREST_CLAMP,
    };
}