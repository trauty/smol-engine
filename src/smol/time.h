#pragma once

#include "defines.h"

namespace smol::time
{
    inline f64 time;
    inline f64 dt;
    inline f64 fixed_dt;

    void update();
} // namespace smol::time