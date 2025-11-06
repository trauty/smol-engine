#pragma once

#include "material.h"
#include "smol/color.h"

namespace smol::asset
{
    template<typename T>
    class asset_ptr_t;

    class texture_asset_t;
} // namespace smol::asset

namespace smol::rendering
{
    class spatial_material_t : public material_t
    {
      public:
        spatial_material_t(smol::asset::asset_ptr_t<smol::asset::shader_asset_t> shader_asset) : material_t(shader_asset) {}

        smol::color_t base_color;
        asset_ptr_t<texture_asset_t> albedo_texture;

        void apply_uniforms();
    };
} // namespace smol::rendering