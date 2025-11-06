#pragma once

#include "defines.h"

namespace smol
{
    struct color_t
    {
        union
        {
            struct
            {
                f32 r, g, b, a;
            };
            f32 data[4];
        };

        color_t() : r(1.0f), g(1.0f), b(1.0f), a(1.0f) {}
        color_t(f32 r, f32 g, f32 b) : r(r), g(g), b(b), a(1.0f) {}
        color_t(f32 r, f32 g, f32 b, f32 a) : r(r), g(g), b(b), a(a) {}
    };
} // namespace smol