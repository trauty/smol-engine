#include "time.h"

#include "smol/defines.h"

#include <SDL3/SDL_timer.h>

namespace smol::time
{
    f64 time;
    f64 dt;
    f64 fixed_dt;

    void update()
    {
        static u64_t freq = SDL_GetPerformanceFrequency();
        const u64_t counter = SDL_GetPerformanceCounter();
        time = static_cast<f64>(counter) / static_cast<f64>(freq);
    }
} // namespace smol::time