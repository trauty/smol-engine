#include "spatial_material.h"

#include "smol/asset/asset.h"
#include "smol/asset/texture.h"
#include "smol/color.h"

namespace smol::rendering
{
    void spatial_material_t::apply_uniforms()
    {
        shader->set_uniform("smol_base_color", base_color);
        shader->bind_texture("smol_albedo_tex", *albedo_texture, 0);
    }
} // namespace smol::rendering