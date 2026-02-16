#pragma once

#include "defines.h"

namespace smol::time
{
    extern f64 time;
    extern f64 dt;
    extern f64 fixed_dt;

    void update();
} // namespace smol::time