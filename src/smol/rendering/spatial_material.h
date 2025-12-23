#pragma once

#include "material.h"
#include "smol/asset/texture.h"
#include "smol/color.h"

namespace smol::rendering
{
    class spatial_material_t : public material_t
    {
      public:
        spatial_material_t(smol::asset_t<smol::shader_asset_t> shader_asset) : material_t(shader_asset) {}

        smol::color_t base_color;
        smol::asset_t<smol::texture_asset_t> albedo_texture;

        void apply_uniforms();
    };
} // namespace smol::rendering