#pragma once

#ifdef SMOL_ENABLE_PROFILING
    #include <tracy/Tracy.hpp>
#else
    #define FrameMark
    #define ZoneScoped
    #define ZoneScopedN(name)
    #define ZoneScopedC(hexcolor)
    #define TracyMessage(txt, size)
    #define TracyMessageL(txt)
    #define TracyAlloc(ptr, size)
    #define TracyFree(ptr)
#endif