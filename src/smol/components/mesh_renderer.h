#pragma once

#include "renderer_component.h"
#include "smol/asset/asset.h"

namespace smol::asset
{
    class mesh_asset_t;
}

namespace smol::rendering
{
    class material_t;
}

namespace smol::components
{
    class mesh_renderer_ct : public renderer_ct
    {
      public:
        void render() const;

        void set_mesh(smol::asset::asset_ptr_t<smol::asset::mesh_asset_t> mesh);
        void set_material(std::shared_ptr<smol::rendering::material_t> material);

      private:
        smol::asset::asset_ptr_t<smol::asset::mesh_asset_t> mesh;
        std::shared_ptr<smol::rendering::material_t> material;
    };
} // namespace smol::components