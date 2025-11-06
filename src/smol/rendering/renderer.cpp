#include "renderer.h"

#include "smol/components/camera.h"
#include "smol/components/renderer_component.h"
#include "smol/components/transform.h"
#include "smol/log.h"
#include "smol/math_util.h"

#include <algorithm>
#include <iostream>
#include <vector>

using namespace smol::components;

namespace smol::renderer
{
    namespace
    {
        std::vector<GLuint> all_shader_programs;

        // Camera UBO
        struct alignas(32) camera_ubo_t
        {
            mat4 smol_view;
            mat4 smol_projection;
            vec3 smol_camera_position;
            f32 padding0;
            vec3 smol_camera_direction;
            f32 padding1;
        };

        constexpr std::size_t OFFSET_VIEW = offsetof(camera_ubo_t, smol_view);
        constexpr std::size_t OFFSET_PROJECTION = offsetof(camera_ubo_t, smol_projection);
        constexpr std::size_t OFFSET_POSITION = offsetof(camera_ubo_t, smol_camera_position);
        constexpr std::size_t OFFSET_DIRECTION = offsetof(camera_ubo_t, smol_camera_direction);

        GLuint camera_binding_point = 0;
        GLuint camera_ubo;
    } // namespace

    void init()
    {
        glGenBuffers(1, &camera_ubo);
        glBindBuffer(GL_UNIFORM_BUFFER, camera_ubo);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(camera_ubo_t), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, camera_binding_point, camera_ubo);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void bind_camera_to_shader(GLuint shader_program)
    {
        if (std::find(all_shader_programs.begin(), all_shader_programs.end(), shader_program) == all_shader_programs.end())
        {
            all_shader_programs.push_back(shader_program);
        }

        GLuint block_index = glGetUniformBlockIndex(shader_program, "smol_camera");
        if (block_index != GL_INVALID_INDEX)
        {
            glUniformBlockBinding(shader_program, block_index, camera_binding_point);
        }
        else
        {
            SMOL_LOG_ERROR("RENDER", "Shader programm '{}' tried to bind to smol_camera UBO without it existing", shader_program);
        }
    }

    void unbind_camera_to_shader(GLuint shader_program)
    {
        std::vector<GLuint>::const_iterator it = std::find(all_shader_programs.begin(), all_shader_programs.end(), shader_program);

        if (it == all_shader_programs.end())
        {
            SMOL_LOG_ERROR("RENDER", "Tried to unbind shader with program id '{}', but none with that id exists", shader_program);
            return;
        }

        all_shader_programs.erase(it);
    }

    void rebind_camera_block_to_all_shaders()
    {
        for (GLuint program : all_shader_programs)
        {
            bind_camera_to_shader(program);
        }
    }

    void render()
    {
        if (camera_ct::main_camera == nullptr)
            return;

        glBindBuffer(GL_UNIFORM_BUFFER, camera_ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, OFFSET_VIEW, sizeof(mat4), camera_ct::main_camera->view_matrix);
        glBufferSubData(GL_UNIFORM_BUFFER, OFFSET_PROJECTION, sizeof(mat4), camera_ct::main_camera->projection_matrix);
        glBufferSubData(GL_UNIFORM_BUFFER, OFFSET_POSITION, sizeof(vec3), camera_ct::main_camera->transform->get_world_position().data);
        glBufferSubData(GL_UNIFORM_BUFFER, OFFSET_DIRECTION, sizeof(vec3), camera_ct::main_camera->transform->get_world_euler_angles().data);

        for (const renderer_ct* renderer : renderer_ct::all_renderers)
        {
            if (renderer->is_active())
            {
                renderer->render();
            }
        }
    }

    void shutdown()
    {
        if (camera_ubo)
            glDeleteBuffers(1, &camera_ubo);
    }
} // namespace smol::renderer