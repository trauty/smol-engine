#pragma once

#include "renderer_component.h"
#include "smol/asset.h"
#include "smol/asset/mesh.h"
#include "smol/rendering/material.h"

namespace smol::components
{
    class mesh_renderer_ct : public renderer_ct
    {
      public:
        void render() const;

        void set_mesh(smol::asset_t<smol::mesh_asset_t> mesh);
        void set_material(std::shared_ptr<smol::rendering::material_t> material);

      private:
        smol::asset_t<smol::mesh_asset_t> mesh;
        std::shared_ptr<smol::rendering::material_t> material;
    };
} // namespace smol::components