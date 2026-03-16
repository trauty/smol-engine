#pragma once

#include "defines.h"

namespace smol
{
    struct SMOL_API color_t
    {
        union
        {
            struct
            {
                f32 r, g, b, a;
            };
            f32 data[4];
        };

        color_t();
        color_t(f32 r, f32 g, f32 b);
        color_t(f32 r, f32 g, f32 b, f32 a);
    };
} // namespace smol