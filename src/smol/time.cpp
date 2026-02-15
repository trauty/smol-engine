#include "time.h"

#include "smol/defines.h"

#include <SDL3/SDL_timer.h>

namespace smol::time
{
    void update()
    {
        static u64_t freq = SDL_GetPerformanceFrequency();
        const u64_t counter = SDL_GetPerformanceCounter();
        time = static_cast<f64>(counter) / static_cast<f64>(freq);
    }
} // namespace smol::time