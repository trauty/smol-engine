#pragma once

#include "asset.h"
#include "smol/defines.h"

using GLuint = u32;

namespace smol::asset
{
    class mesh_asset_t : public asset_t
    {
      public:
        mesh_asset_t(const std::string& path);
        ~mesh_asset_t();

        GLuint get_vao() const;
        i32 get_vertex_count() const;
        i32 get_index_count() const;
        bool has_indices() const;

      private:
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        i32 vertex_count = 0;
        i32 index_count = 0;
        bool uses_indices = false;
    };
} // namespace smol::asset