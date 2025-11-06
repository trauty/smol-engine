#include "mesh.h"

#include "smol/log.h"
#include "smol/main_thread.h"

#include <glad/gl.h>
#include <tinygltf/tiny_gltf.h>

namespace smol::asset
{
    struct vertex_t
    {
        f32 position[3];
        f32 normal[3];
        f32 uv[2];
    };

    mesh_asset_t::mesh_asset_t(const std::string& path)
    {
        tinygltf::TinyGLTF loader;
        tinygltf::Model model;
        std::string warn, err;

        bool is_glb = path.ends_with(".glb");
        bool loaded = is_glb ? loader.LoadBinaryFromFile(&model, &err, &warn, path) : loader.LoadASCIIFromFile(&model, &err, &warn, path);

        if (!loaded)
        {
            SMOL_LOG_ERROR("MESH", "GLTF load error: {}", err);
            return;
        }

        if (model.meshes.empty())
        {
            SMOL_LOG_ERROR("MESH", "GLTF file has no meshes: {}", path);
            return;
        }

        const tinygltf::Mesh& mesh = model.meshes[0];
        const tinygltf::Primitive& primitive = mesh.primitives[0];
        const std::map<std::string, int>& attribs = primitive.attributes;

        const tinygltf::Accessor& pos_accessor = model.accessors[primitive.attributes.at("POSITION")];
        const tinygltf::BufferView& pos_view = model.bufferViews[pos_accessor.bufferView];
        const tinygltf::Buffer& pos_buffer = model.buffers[pos_view.buffer];
        const f32* positions = reinterpret_cast<const f32*>(&pos_buffer.data[pos_view.byteOffset + pos_accessor.byteOffset]);

        vertex_count = pos_accessor.count;

        const f32* normals = nullptr;
        bool has_normals = attribs.find("NORMAL") != attribs.end();
        if (has_normals)
        {
            const tinygltf::Accessor& norm_accessor = model.accessors[attribs.at("NORMAL")];
            const tinygltf::BufferView& norm_view = model.bufferViews[norm_accessor.bufferView];
            //const tinygltf::Buffer& norm_buffer = model.buffers[norm_view.buffer];
            normals = reinterpret_cast<const f32*>(&pos_buffer.data[norm_view.byteOffset + norm_accessor.byteOffset]);
        }

        const f32* uvs = nullptr;
        bool has_uvs = attribs.find("TEXCOORD_0") != attribs.end();
        if (has_uvs)
        {
            const tinygltf::Accessor& uv_accessor = model.accessors[attribs.at("TEXCOORD_0")];
            const tinygltf::BufferView& uv_view = model.bufferViews[uv_accessor.bufferView];
            //const tinygltf::Buffer& uv_buffer = model.buffers[uv_view.buffer];
            uvs = reinterpret_cast<const f32*>(&pos_buffer.data[uv_view.byteOffset + uv_accessor.byteOffset]);
        }

        // not very efficient => 3 single vbos would be better
        std::vector<vertex_t> vertex_data(vertex_count);
        for (i32 i = 0; i < vertex_count; ++i)
        {
            vertex_t& v = vertex_data[i];
            std::memcpy(v.position, &positions[i * 3], 3 * sizeof(f32));
            if (has_normals)
                std::memcpy(v.normal, &normals[i * 3], 3 * sizeof(f32));
            else
                std::memset(v.normal, 0, 3 * sizeof(f32));

            if (has_uvs)
                std::memcpy(v.uv, &uvs[i * 2], 2 * sizeof(f32));
            else
                std::memset(v.uv, 0, 2 * sizeof(f32));
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
                    return;
                }
            }

            index_count = static_cast<i32>(indices.size());
            uses_indices = true;
        }

        smol::main_thread::enqueue([this, vertex_data = std::move(vertex_data), indices = std::move(indices)]() mutable
                                   {
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);

            glGenBuffers(1, &vbo);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, vertex_data.size() * sizeof(vertex_t), vertex_data.data(), GL_STATIC_DRAW);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*)offsetof(vertex_t, position));

            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*)offsetof(vertex_t, normal));

            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*)offsetof(vertex_t, uv));

            if (!indices.empty())
            {
                glGenBuffers(1, &ebo);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(u32), indices.data(), GL_STATIC_DRAW);
            }

            glBindVertexArray(0); });
    }

    mesh_asset_t::~mesh_asset_t()
    {
        smol::main_thread::enqueue([this]()
                                   {
            if (vbo) glDeleteBuffers(1, &vbo);
            if (ebo) glDeleteBuffers(1, &ebo);
            if (vao) glDeleteVertexArrays(1, &vao); });
    }

    GLuint mesh_asset_t::get_vao() const
    {
        return vao;
    }
    i32 mesh_asset_t::get_vertex_count() const
    {
        return vertex_count;
    }
    i32 mesh_asset_t::get_index_count() const
    {
        return index_count;
    }
    bool mesh_asset_t::has_indices() const
    {
        return uses_indices;
    }
} // namespace smol::asset
