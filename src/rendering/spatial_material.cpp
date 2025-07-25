#include "spatial_material.h"

#include "color.h"
#include "asset/asset.h"
#include "asset/texture.h"

namespace smol::rendering
{
    void spatial_material_t::apply_uniforms()
    {
        shader->set_uniform("smol_base_color", base_color);
        //shader->bind_texture("smol_albedo_tex", *albedo_texture, 0);
    }
}