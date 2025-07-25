#include "mesh_renderer.h"

#include <glad/gl.h>

#include "rendering/material.h"
#include "asset/shader.h"
#include "asset/mesh.h"
#include "core/gameobject.h"

#include "color.h"

namespace smol::components
{
    void mesh_renderer_ct::render() const
    {
        if (!mesh || !material) return;

        material->shader->bind();

        mat4_t& model_mat = get_gameobject()->get_transform()->get_world_matrix();

        material->set_uniform("smol_model_mat", model_mat);
        material->apply_uniforms();

        glBindVertexArray(mesh->get_vao());
        if (mesh->has_indices())
        {
            glDrawElements(GL_TRIANGLES, mesh->get_index_count(), GL_UNSIGNED_INT, 0);
        }
        else
        {
            glDrawArrays(GL_TRIANGLES, 0, mesh->get_vertex_count());
        }
        glBindVertexArray(0);
    }

    void mesh_renderer_ct::set_mesh(smol::asset::asset_ptr_t<smol::asset::mesh_asset_t> mesh)
    {
        this->mesh = mesh;
    }

    void mesh_renderer_ct::set_material(std::shared_ptr<smol::rendering::material_t> material)
    {
        this->material = material;
    }
}