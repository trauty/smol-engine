#pragma once

#include "smol/asset.h"
#include "smol/defines.h"
#include <memory>
#include <optional>
#include <string>

namespace smol
{
    struct mesh_render_data_t
    {
        u32 vao = 0;
        u32 vbo = 0;
        u32 ebo = 0;

        ~mesh_render_data_t();
    };

    struct mesh_asset_t
    {
        std::shared_ptr<mesh_render_data_t> mesh_data = std::make_shared<mesh_render_data_t>();

        i32 vertex_count = 0;
        i32 index_count = 0;
        bool uses_indices = false;

        u32 vao() const { return mesh_data->vao; }
        bool ready() const { return mesh_data->vao != 0; }
    };

    template<>
    struct asset_loader_t<mesh_asset_t>
    {
        static std::optional<mesh_asset_t> load(const std::string& path);
    };
} // namespace smol