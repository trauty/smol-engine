#include "renderer.h"

#include "smol/asset/mesh.h"
#include "smol/components/camera.h"
#include "smol/components/renderer.h"
#include "smol/components/transform.h"
#include "smol/core/gameobject.h"
#include "smol/log.h"
#include "smol/math_util.h"
#include "smol/rendering/material.h"
#include "smol/rendering/ubo.h"

#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cglm/mat4.h>
#include <cglm/vec3.h>
#include <iostream>
#include <vector>

using namespace smol::components;

namespace smol::renderer
{
    namespace
    {
        std::vector<GLuint> all_shader_programs;

        // Camera UBO
        struct alignas(32) camera_data_t
        {
            mat4 smol_view;
            mat4 smol_projection;
            vec3 smol_camera_position;
            f32 padding0;
            vec3 smol_camera_direction;
            f32 padding1;
        };

        ubo_t cam_buf;

        struct object_data_t
        {
            mat4 smol_model_matrix;
        };

        ubo_t obj_buf;
    } // namespace

    void init()
    {
        cam_buf.init(sizeof(camera_data_t));
        obj_buf.init(sizeof(object_data_t));
    }

    void render()
    {
        return;
        if (camera_ct::main_camera == nullptr) { return; }

        camera_ct* cam = camera_ct::main_camera;

        camera_data_t cam_data;
        glm_mat4_copy(cam->view_matrix.data, cam_data.smol_view);
        glm_mat4_copy(cam->projection_matrix.data, cam_data.smol_projection);
        glm_vec3_copy(cam->transform->get_world_position().data, cam_data.smol_camera_position);
        glm_vec3_copy(cam->transform->get_world_euler_angles().data, cam_data.smol_camera_direction);

        cam_buf.update(&cam_data, sizeof(camera_data_t));
        cam_buf.bind(0);

        for (const renderer_ct* renderer : renderer_ct::all_renderers)
        {
            if (!renderer->is_active()) { continue; }

            const material_t& mat = renderer->material;
            const mesh_asset_t& mesh = renderer->mesh;

            if (!mat.shader.ready() || !mesh.ready()) { continue; }

            mat.shader.bind();

            if (mat.ubo.ready()) { mat.ubo.bind(1); }

            // textures (no explicit bindings in opengl 3.3)
            for (size_t i = 0; i < mat.active_textures.size(); ++i)
            {
                const texture_binding_t& bind = mat.active_textures[i];

                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, bind.id);
                glUniform1i(bind.location, (i32)i);
            }

            object_data_t obj_data;
            glm_mat4_copy(renderer->get_gameobject()->get_transform()->get_world_matrix().data,
                          obj_data.smol_model_matrix);
            obj_buf.update(&obj_data, sizeof(object_data_t));
            obj_buf.bind(2);

            glBindVertexArray(mesh.vao());
            // need to force the use of the ebo, this just assumes it exists, which it does in most cases (still bad)
            glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, 0);
        }
    }
} // namespace smol::renderer