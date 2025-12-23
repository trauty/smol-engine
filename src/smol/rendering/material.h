#pragma once
#include "smol/asset.h"
#include "smol/asset/shader.h"
#include "smol/defines.h"
#include "smol/math_util.h"

#include <memory>
#include <variant>

namespace smol::rendering
{
    class material_t
    {
      public:
        material_t() = default;
        material_t(smol::asset_t<smol::shader_asset_t> shader_asset);
        ~material_t() = default;

        void set_uniform(const std::string& name, uniform_value_t value);
        virtual void apply_uniforms() = 0;

        smol::asset_t<smol::shader_asset_t> shader;
    };
} // namespace smol::rendering