#pragma once

#include "smol/defines.h"

namespace smol::renderer
{
    constexpr u32_t MAX_SAMPLED_TEXTURES = 100000;
    constexpr u32_t MAX_STORAGE_TEXTURES = 4096;
    constexpr u32_t MAX_SSBOS = 100000;
    constexpr u32_t MAX_SAMPLERS = 32;

    constexpr u32_t MAX_MATERIAL_COUNT = 4096;
    constexpr u32_t MAX_MATERIAL_BUFFER_SIZE = MAX_MATERIAL_COUNT * 512;

    constexpr u32_t BINDLESS_NULL_HANDLE = 0xffffffff;

    constexpr u32_t MAX_FRAMES_IN_FLIGHT = 2;

    constexpr u32_t MAX_LIGHTS = 1024;
    constexpr u32_t MAX_DIR_LIGHTS = 32;
} // namespace smol::renderer