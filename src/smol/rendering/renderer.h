#pragma once

#include "smol/defines.h"

#include <glad/gl.h>

namespace smol::renderer
{
    enum class shader_stage_e
    {
        VERTEX,
        FRAGMENT,
        GEOMETRY, // <-- nicht geplant
        COMPUTE // <-- nicht geplant
    };

    void bind_camera_to_shader(GLuint shader_program);
    void unbind_camera_to_shader(GLuint shader_program);
    void rebind_camera_block_to_all_shaders();

    void init();
    void render();
    void shutdown();
} // namespace smol::renderer