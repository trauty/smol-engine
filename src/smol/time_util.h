#pragma once

#include "defines.h"

namespace smol::time
{
    SMOL_API f64 get_delta_time();
    void set_delta_time(f64 delta_time);

    SMOL_API f64 get_fixed_delta_time();
    void set_fixed_delta_time(f64 fixed_delta_time);

    SMOL_API f64 get_time_in_seconds();
} // namespace smol::time