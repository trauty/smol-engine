#include "mesh.h"

#include "smol/asset.h"
#include "smol/log.h"
#include "smol/main_thread.h"

#include <glad/gl.h>
#include <optional>
#include <tinygltf/tiny_gltf.h>

namespace smol
{
    struct vertex_t
    {
        f32 position[3];
        f32 normal[3];
        f32 uv[2];
    };

    mesh_render_data_t::~mesh_render_data_t()
    {
        u32 c_vao = vao, c_vbo = vbo, c_ebo = ebo;

        if (c_vao != 0 || c_vbo != 0 || c_ebo != 0)
        {
            smol::main_thread::enqueue([c_vao, c_vbo, c_ebo]() {
                if (c_vbo) glDeleteBuffers(1, &c_vbo);
                if (c_ebo) glDeleteBuffers(1, &c_ebo);
                if (c_vao) glDeleteVertexArrays(1, &c_vao);
            });
        }
    }

    std::optional<mesh_asset_t> asset_loader_t<mesh_asset_t>::load(const std::string& path)
    {
        tinygltf::TinyGLTF loader;
        tinygltf::Model model;
        std::string warn, err;

        bool is_glb = path.ends_with(".glb");
        bool loaded = is_glb ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
                             : loader.LoadASCIIFromFile(&model, &err, &warn, path);

        if (!loaded)
        {
            SMOL_LOG_ERROR("MESH", "GLTF load error: {}", err);
            return std::nullopt;
        }

        if (model.meshes.empty())
        {
            SMOL_LOG_ERROR("MESH", "GLTF file has no meshes: {}", path);
            return std::nullopt;
        }

        mesh_asset_t mesh_asset;

        const tinygltf::Mesh& mesh = model.meshes[0];
        const tinygltf::Primitive& primitive = mesh.primitives[0];
        const std::map<std::string, int>& attribs = primitive.attributes;

        const tinygltf::Accessor& pos_accessor = model.accessors[primitive.attributes.at("POSITION")];
        const tinygltf::BufferView& pos_view = model.bufferViews[pos_accessor.bufferView];
        const tinygltf::Buffer& pos_buffer = model.buffers[pos_view.buffer];
        const f32* positions =
            reinterpret_cast<const f32*>(&pos_buffer.data[pos_view.byteOffset + pos_accessor.byteOffset]);

        mesh_asset.vertex_count = pos_accessor.count;

        const f32* normals = nullptr;
        const bool has_normals = attribs.find("NORMAL") != attribs.end();
        if (has_normals)
        {
            const tinygltf::Accessor& norm_accessor = model.accessors[attribs.at("NORMAL")];
            const tinygltf::BufferView& norm_view = model.bufferViews[norm_accessor.bufferView];
            const tinygltf::Buffer& norm_buffer = model.buffers[norm_view.buffer];
            normals = reinterpret_cast<const f32*>(&norm_buffer.data[norm_view.byteOffset + norm_accessor.byteOffset]);
        }

        const f32* uvs = nullptr;
        const bool has_uvs = attribs.find("TEXCOORD_0") != attribs.end();
        if (has_uvs)
        {
            const tinygltf::Accessor& uv_accessor = model.accessors[attribs.at("TEXCOORD_0")];
            const tinygltf::BufferView& uv_view = model.bufferViews[uv_accessor.bufferView];
            const tinygltf::Buffer& uv_buffer = model.buffers[uv_view.buffer];
            uvs = reinterpret_cast<const f32*>(&uv_buffer.data[uv_view.byteOffset + uv_accessor.byteOffset]);
        }

        // not very efficient => 3 single vbos would be better
        std::vector<vertex_t> vertex_data(mesh_asset.vertex_count);
        for (i32 i = 0; i < mesh_asset.vertex_count; ++i)
        {
            vertex_t& v = vertex_data[i];
            std::memcpy(v.position, &positions[i * 3], 3 * sizeof(f32));
            if (has_normals) std::memcpy(v.normal, &normals[i * 3], 3 * sizeof(f32));
            else std::memset(v.normal, 0, 3 * sizeof(f32));

            if (has_uvs) std::memcpy(v.uv, &uvs[i * 2], 2 * sizeof(f32));
            else std::memset(v.uv, 0, 2 * sizeof(f32));
        }

        std::vector<u32> indices;
        if (primitive.indices >= 0)
        {
            const tinygltf::Accessor& idx_accessor = model.accessors[primitive.indices];
            const tinygltf::BufferView& idx_view = model.bufferViews[idx_accessor.bufferView];
            const tinygltf::Buffer& idx_buffer = model.buffers[idx_view.buffer];

            const void* raw_indices = &idx_buffer.data[idx_view.byteOffset + idx_accessor.byteOffset];
            indices.resize(idx_accessor.count);

            switch (idx_accessor.componentType)
            {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    {
                        const u16* src = reinterpret_cast<const u16*>(raw_indices);
                        for (size_t i = 0; i < indices.size(); ++i) indices[i] = static_cast<u32>(src[i]);
                        break;
                    }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    {
                        const u32* src = reinterpret_cast<const u32*>(raw_indices);
                        std::copy(src, src + idx_accessor.count, indices.begin());
                        break;
                    }
                default:
                    {
                        SMOL_LOG_ERROR("MESH", "Unsupported index type in asset: {}", path);
                        return std::nullopt;
                    }
            }

            mesh_asset.index_count = static_cast<i32>(indices.size());
            mesh_asset.uses_indices = true;
        }

        smol::main_thread::enqueue([mesh_data = mesh_asset.mesh_data, vertex_data = std::move(vertex_data),
                                    indices = std::move(indices)]() mutable {
            glGenVertexArrays(1, &mesh_data->vao);
            glBindVertexArray(mesh_data->vao);

            glGenBuffers(1, &mesh_data->vbo);
            glBindBuffer(GL_ARRAY_BUFFER, mesh_data->vbo);
            glBufferData(GL_ARRAY_BUFFER, vertex_data.size() * sizeof(vertex_t), vertex_data.data(), GL_STATIC_DRAW);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*)offsetof(vertex_t, position));

            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*)offsetof(vertex_t, normal));

            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*)offsetof(vertex_t, uv));

            if (!indices.empty())
            {
                glGenBuffers(1, &mesh_data->ebo);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh_data->ebo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(u32), indices.data(), GL_STATIC_DRAW);
            }

            glBindVertexArray(0);
        });

        return mesh_asset;
    }
} // namespace smol
