#include "material.h"

#include "smol/asset/shader.h"
#include "smol/defines.h"

namespace smol::rendering
{
    material_t::material_t(asset_ptr_t<shader_asset_t> shader_asset)
    {
        this->shader = shader_asset;
    }

    void material_t::set_uniform(const std::string& name, uniform_value_t value)
    {
        shader->set_uniform(name, value);
    }
} // namespace smol::rendering