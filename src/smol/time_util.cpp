#include "time_util.h"

#include "smol/defines.h"

#include <SDL3/SDL_timer.h>

namespace smol::time
{
    namespace
    {
        f64 delta_time;
        f64 fixed_delta_time;
    } // namespace

    f64 get_delta_time()
    {
        return delta_time;
    }

    void set_delta_time(f64 new_delta_time)
    {
        delta_time = new_delta_time;
    }

    SMOL_API f64 get_fixed_delta_time()
    {
        return fixed_delta_time;
    }

    void set_fixed_delta_time(f64 new_fixed_delta_time)
    {
        fixed_delta_time = new_fixed_delta_time;
    }

    f64 get_time_in_seconds()
    {
        static u64_t freq = SDL_GetPerformanceFrequency();
        const u64_t counter = SDL_GetPerformanceCounter();

        return static_cast<f64>(counter) / static_cast<f64>(freq);
    }
} // namespace smol::time