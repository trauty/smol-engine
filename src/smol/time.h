#pragma once

#include "defines.h"

namespace smol::time
{
    extern f64 time;
    extern f64 dt;
    extern f64 fixed_dt;

    void update();

    SMOL_API f64 get_time();
    SMOL_API f64 get_dt();
    SMOL_API f64 get_fixed_dt();
} // namespace smol::time