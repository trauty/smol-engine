#pragma once

#include "smol/defines.h"

#include <glad/gl.h>

namespace smol::renderer
{
    enum class shader_stage_e
    {
        VERTEX,
        FRAGMENT,
        GEOMETRY, // <-- not implemented
        COMPUTE   // <-- not implemented
    };

    void init();
    void render();
} // namespace smol::renderer