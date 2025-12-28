#pragma once

#include "smol/asset/mesh.h"
#include "smol/core/component.h"
#include "smol/defines.h"
#include "smol/math_util.h"
#include "smol/rendering/material.h"

#include <vector>

namespace smol::components
{
    class renderer_ct : public smol::core::component_t
    {
      public:
        static std::vector<renderer_ct*> all_renderers;

        smol::mesh_asset_t mesh;
        smol::material_t material;

        renderer_ct();
        ~renderer_ct();

        void set_mesh(const smol::mesh_asset_t& new_mesh);
        void set_material(const smol::material_t& new_mat);
    };
} // namespace smol::components