#pragma once
#include "defines.h"

#include <memory>
#include <variant>

#include "asset/asset.h"
#include "math_util.h"
#include "asset/shader.h"

using namespace smol::asset;

namespace smol::rendering
{
    class material_t
    {   
    public:
        material_t() = default;
        material_t(asset_ptr_t<shader_asset_t> shader_asset);
        ~material_t() = default;

        void set_uniform(const std::string& name, uniform_value_t value);
        virtual void apply_uniforms() = 0;

        asset_ptr_t<shader_asset_t> shader;
    };
}